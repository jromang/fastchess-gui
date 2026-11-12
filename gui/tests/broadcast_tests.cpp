// Broadcast outputs: JSON, live PGN, the TLCS crosstable layout, the TLCS server and the web page.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <thread>

#include <json.hpp>

#include <fmt/include/fmt/format.h>

#include "broadcast.hpp"
#include "net.hpp"

#ifdef _WIN32
#    include <windows.h>
#endif

using namespace fcg;
using namespace std::chrono_literals;

namespace {

BroadcastSnapshot sample() {
    BroadcastSnapshot s;
    s.name   = "Test broadcast";
    s.format = "round_robin";
    s.played = 3;
    s.total  = 12;

    LiveGame g;
    g.slot          = 0;
    g.number        = 4;
    g.white         = "Alpha";
    g.black         = "Beta";
    g.start_fen     = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    g.san           = {"e4", "e5", "Nf3"};
    g.uci           = {"e2e4", "e7e5", "g1f3"};
    g.fen           = "rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2";
    g.white_to_move = false;
    g.white_elo     = 3500;
    g.clocks        = true;
    g.white_ms      = 59000;
    g.black_ms      = 58000;
    g.active        = true;
    g.search[1]     = {12, -25, 123456, 1500, {"b8c6", "f1b5"}};
    s.games.push_back(g);

    fcg::tour::TournamentSpec spec;
    for (int i = 0; i < 3; ++i) {
        fcg::tour::Participant p;
        p.id          = i;
        p.engine.name = std::string(1, char('A' + i)) + "lpha";
        spec.participants.push_back(p);
    }
    std::vector<fcg::tour::GameResult> results(3);
    results[0].white = 0, results[0].black = 1, results[0].result = "1-0";
    results[1].white = 1, results[1].black = 2, results[1].result = "1/2-1/2";
    results[2].white = 2, results[2].black = 0, results[2].result = "0-1";
    s.table        = fcg::tour::computeTable(spec, results);
    s.recent       = {"Calpha - Alpha 0-1"};
    s.played_games = {{3, "Calpha", "Alpha", "0-1"}, {2, "Balpha", "Calpha", "1/2-1/2"}, {1, "Alpha", "Balpha", "1-0"}};
    return s;
}

}  // namespace

TEST_CASE("broadcast: JSON and live PGN") {
    const auto s    = sample();
    const auto json = nlohmann::json::parse(snapshotJson(s));
    CHECK(json["name"] == "Test broadcast");
    REQUIRE(json["games"].size() == 1);
    CHECK(json["games"][0]["moves"].size() == 3);
    CHECK(json["games"][0]["black_search"]["depth"] == 12);
    REQUIRE(json["standings"].size() == 3);
    CHECK(json["standings"][0]["name"] == "Alpha");
    CHECK(json["standings"][0]["crosstable"][0].is_null());

    const auto pgn = livePgn(s);
    CHECK(pgn.find("[White \"Alpha\"]") != std::string::npos);
    CHECK(pgn.find("[WhiteElo \"3500\"]") != std::string::npos);
    CHECK(pgn.find("[BlackElo") == std::string::npos);  // an engine without a rating gets no tag
    CHECK(json["games"][0]["white_elo"] == 3500);
    CHECK(pgn.find("1. e4 e5 2. Nf3 *") != std::string::npos);
    CHECK(pgn.find("[FEN") == std::string::npos);  // the initial position needs none

    // Quotes and backslashes in a name are escaped.
    auto odd           = s;
    odd.name           = "The \"big\" one";
    odd.games[0].white = "C:\\engines\\Alpha";
    const auto escaped = livePgn(odd);
    CHECK(escaped.find("[Event \"The \\\"big\\\" one\"]") != std::string::npos);
    CHECK(escaped.find("[White \"C:\\\\engines\\\\Alpha\"]") != std::string::npos);
}

TEST_CASE("broadcast: the crosstable keeps the columns node-tlcv reads") {
    const auto lines = tlcsCrosstable(sample(), 3);
    REQUIRE(lines.size() >= 5);
    const auto& header = lines[0];
    CHECK(header.rfind("RANK", 0) == 0);
    const auto games_col = header.find("GAMES");
    REQUIRE(games_col != std::string::npos);
    REQUIRE(header.find("POINTS") != std::string::npos);

    // No row starts with a space (a TLCS client trims what follows "CT:"), and games and points sit under their
    // header.
    const auto& first = lines[2];
    CHECK(first.rfind("1.", 0) == 0);
    CHECK(std::stoi(first.substr(games_col)) == 2);
    CHECK(first.find("**") != std::string::npos);
    CHECK(std::find(lines.begin(), lines.end(), "total games = 3") != lines.end());

    // The games played, newest first, read as node-tlcv's parseGames reads them: its game numbers follow them.
    const auto heading = std::find_if(lines.begin(), lines.end(), [](const std::string& l) {
        return std::regex_search(l, std::regex(R"(game\s+no\.)", std::regex::icase));
    });
    REQUIRE(heading != lines.end());
    const std::regex row(R"(^\s*(\d+)\s+(.+?)\s{3,}(.+?)\s{3,}(.+?)\s*$)");
    std::vector<std::string> games;
    for (auto it = heading + 1; it != lines.end(); ++it) {
        std::smatch m;
        if (std::regex_match(*it, m, row))
            games.push_back(m[1].str() + " " + m[2].str() + "|" + m[3].str() + "|" + m[4].str());
    }
    REQUIRE(games.size() == 3);
    CHECK(games[0] == "3 Calpha|Alpha|0-1");
    CHECK(games[1] == "2 Balpha|Calpha|1/2-1/2");
    CHECK(games[2] == "1 Alpha|Balpha|1-0");
}

TEST_CASE("broadcast: a TLCS client logs on and receives the game, the crosstable, and answers to PING") {
    // What Tom's Live Chess Viewer 2.1 needs, from node-tlcv's docs/official-client.md: LOGON SUCCESSFUL before
    // anything counts, FEATURE: PING for its keepalive, its own numbered PING acknowledged, "XTIME: <cs> otim <cs>",
    // and the FEN after each move, sent before the move.
    TlcsServer server;
    const uint16_t port = 26017;
    REQUIRE(server.start(port).empty());
    server.publish(sample(), -1);
    std::this_thread::sleep_for(300ms);

    net::UdpSocket client;
    REQUIRE(client.bind(26018).empty());
    const net::Endpoint to{0x7F000001, port};
    client.send(to, "LOGONv15:tester");

    std::vector<std::string> received;
    const auto until = std::chrono::steady_clock::now() + 2s;
    bool pinged      = false;
    while (std::chrono::steady_clock::now() < until) {
        net::Endpoint from;
        const auto msg = client.receive(from, 100);
        if (!msg) {
            if (!pinged) client.send(to, "< 7>PING"), pinged = true;  // numbered, the way TLCV sends it
            continue;
        }
        if (msg->front() == '<') {
            const auto close = msg->find('>');
            client.send(to, "ACK: " + msg->substr(1, close - 1));
            received.push_back(msg->substr(close + 1));
        } else {
            received.push_back(*msg);
        }
    }
    CHECK(server.clients() == 1);

    const auto has = [&](const std::string& text) {
        return std::find(received.begin(), received.end(), text) != received.end();
    };
    REQUIRE_FALSE(received.empty());
    CHECK(received.front() == "LOGON SUCCESSFUL");
    CHECK(has("FEATURE: PING"));
    CHECK(has("SITE: Test broadcast"));
    CHECK(has("WPLAYER: Alpha"));
    CHECK(has("BPLAYER: Beta"));
    CHECK(has("FEN: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq"));
    // The players before the initial position, which node-tlcv names its PGN with.
    const auto index = [&](const std::string& text) {
        return std::find(received.begin(), received.end(), text) - received.begin();
    };
    CHECK(index("BPLAYER: Beta") < index("FEN: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq"));
    CHECK(has("WMOVE: 1. e4"));
    CHECK(has("BMOVE: 1. e5"));
    CHECK(has("WMOVE: 2. Nf3"));
    CHECK(has("BTIME: 5800 otim 5900"));  // Black to move
    CHECK(has("FMR: 1"));

    // The position after a move comes right before it.
    const auto e4 = std::find(received.begin(), received.end(), "WMOVE: 1. e4");
    REQUIRE(e4 != received.end());
    REQUIRE(e4 != received.begin());
    CHECK(*(e4 - 1) == "FEN: rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq");
    CHECK(has("CTRESET"));
    CHECK(has("CT: total games = 3"));
    CHECK(has("PONG"));
    CHECK(has("ACK: 7"));

    server.stop();
}

TEST_CASE("broadcast: TLCS tells the end of the game it follows, even once the next game took its slot") {
    TlcsServer server;
    const uint16_t port = 26019;
    REQUIRE(server.start(port).empty());
    server.publish(sample(), -1);
    std::this_thread::sleep_for(300ms);

    net::UdpSocket client;
    REQUIRE(client.bind(26020).empty());
    const net::Endpoint to{0x7F000001, port};
    client.send(to, "LOGONv15:tester");

    std::vector<std::string> received;
    const auto listen = [&](std::chrono::milliseconds how_long) {
        const auto until = std::chrono::steady_clock::now() + how_long;
        while (std::chrono::steady_clock::now() < until) {
            net::Endpoint from;
            const auto msg = client.receive(from, 50);
            if (!msg) continue;
            if (msg->front() == '<') {
                const auto close = msg->find('>');
                client.send(to, "ACK: " + msg->substr(1, close - 1));
                received.push_back(msg->substr(close + 1));
            } else {
                received.push_back(*msg);
            }
        }
    };
    listen(600ms);

    // Game 4 ended with 2...Nc6 3.Bb5 1-0 and game 5 already started in its slot: game 4 is only among the ended.
    auto next            = sample();
    auto ended           = next.games[0];
    ended.san            = {"e4", "e5", "Nf3", "Nc6", "Bb5"};
    ended.uci            = {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5"};
    ended.active         = false;
    ended.result         = "1-0";
    next.ended           = {ended};
    next.games[0]        = sample().games[0];
    next.games[0].number = 5;
    next.games[0].white  = "Gamma";
    next.games[0].san.clear();
    next.games[0].uci.clear();
    server.publish(next, -1);
    listen(800ms);
    server.publish(next, -1);  // the following snapshot moves on to game 5
    listen(800ms);

    const auto at = [&](const std::string& text) { return std::find(received.begin(), received.end(), text); };
    REQUIRE(at("WMOVE: 3. Bb5") != received.end());
    REQUIRE(at("result: 1-0") != received.end());
    REQUIRE(at("WPLAYER: Gamma") != received.end());
    CHECK(at("BMOVE: 2. Nc6") < at("WMOVE: 3. Bb5"));
    CHECK(at("WMOVE: 3. Bb5") < at("result: 1-0"));
    CHECK(at("result: 1-0") < at("WPLAYER: Gamma"));

    server.stop();
}

TEST_CASE("broadcast: TLCS sends one numbered command at a time, in order, the PV lines unnumbered") {
    // node-tlcv drops a numbered command lower than one it already has: the next one waits for the ACK.
    TlcsServer server;
    const uint16_t port = 26021;
    REQUIRE(server.start(port).empty());
    server.publish(sample(), -1);
    std::this_thread::sleep_for(300ms);

    net::UdpSocket client;
    REQUIRE(client.bind(26022).empty());
    const net::Endpoint to{0x7F000001, port};
    client.send(to, "LOGONv15:tester");

    // Without an ACK: the first command only, sent again.
    std::vector<std::string> numbered;
    auto until = std::chrono::steady_clock::now() + 1500ms;
    while (std::chrono::steady_clock::now() < until) {
        net::Endpoint from;
        if (const auto msg = client.receive(from, 50); msg && msg->front() == '<') numbered.push_back(*msg);
    }
    REQUIRE(numbered.size() >= 2);
    for (const auto& m : numbered) CHECK(m == numbered.front());
    REQUIRE(numbered.front().find(">FEATURE: PING") != std::string::npos);
    const auto first = static_cast<uint32_t>(std::stoul(numbered.front().substr(1)));

    // With ACKs: every number once, in order.
    std::vector<uint32_t> ids;
    client.send(to, fmt::format("ACK: {}", first));
    until = std::chrono::steady_clock::now() + 1500ms;
    while (std::chrono::steady_clock::now() < until) {
        net::Endpoint from;
        const auto msg = client.receive(from, 50);
        if (!msg || msg->front() != '<') continue;
        const auto id = static_cast<uint32_t>(std::stoul(msg->substr(1)));
        ids.push_back(id);
        client.send(to, fmt::format("ACK: {}", id));
    }
    REQUIRE(ids.size() > 5);
    for (std::size_t i = 0; i < ids.size(); ++i) CHECK(ids[i] == first + 1 + i);
    server.stop();
}

TEST_CASE("broadcast: TLCS answers a logon with a few bytes until the client proves its address") {
    // A LOGON may come from a forged address: the game and the crosstable only go to a client that acknowledges
    // the number of the first command, which it can only know by receiving it.
    TlcsServer server;
    const uint16_t port = 26023;
    REQUIRE(server.start(port).empty());
    server.publish(sample(), -1);
    std::this_thread::sleep_for(300ms);

    net::UdpSocket client;
    REQUIRE(client.bind(26024).empty());
    const net::Endpoint to{0x7F000001, port};
    client.send(to, "LOGONv15:tester");
    std::vector<std::string> received;
    const auto listen = [&](std::chrono::milliseconds how_long) {
        const auto until = std::chrono::steady_clock::now() + how_long;
        while (std::chrono::steady_clock::now() < until) {
            net::Endpoint from;
            if (const auto msg = client.receive(from, 50)) received.push_back(*msg);
        }
    };
    listen(400ms);
    REQUIRE(received.size() == 2);
    CHECK(received[0] == "LOGON SUCCESSFUL");
    REQUIRE(received[1].find(">FEATURE: PING") != std::string::npos);
    const auto first = static_cast<uint32_t>(std::stoul(received[1].substr(1)));
    CHECK(server.clients() == 0);

    // A wrong number, a PING, a RESULTTABLE: still nothing but the answer to the PING.
    received.clear();
    client.send(to, fmt::format("ACK: {}", first + 1));
    client.send(to, "RESULTTABLE");
    client.send(to, "PING");
    listen(400ms);
    for (const auto& m : received) CHECK((m == "PONG" || m.find(">FEATURE: PING") != std::string::npos));
    CHECK(server.clients() == 0);

    // The right number: the rest comes.
    received.clear();
    client.send(to, fmt::format("ACK: {}", first));
    listen(400ms);
    CHECK(server.clients() == 1);
    CHECK(std::find(received.begin(), received.end(), "CTRESET") != received.end());
    CHECK(std::any_of(received.begin(), received.end(),
                      [](const std::string& m) { return m.find(">SITE: Test broadcast") != std::string::npos; }));

    // Others not even logged on get no answer, not even an ACK.
    net::UdpSocket stranger;
    REQUIRE(stranger.bind(0).empty());
    stranger.send(to, "<5>PING");
    stranger.send(to, "RESULTTABLE");
    net::Endpoint from;
    CHECK_FALSE(stranger.receive(from, 300).has_value());
    server.stop();
}

TEST_CASE("broadcast: TLCS logons are limited in rate") {
    TlcsServer server;
    const uint16_t port = 26025;
    REQUIRE(server.start(port).empty());
    std::this_thread::sleep_for(200ms);

    // 30 logons at once from as many addresses (ports): 10 answered.
    const net::Endpoint to{0x7F000001, port};
    std::vector<std::unique_ptr<net::UdpSocket>> clients;
    for (int i = 0; i < 30; ++i) {
        clients.push_back(std::make_unique<net::UdpSocket>());
        REQUIRE(clients.back()->bind(0).empty());
        clients.back()->send(to, "LOGONv15:tester");
    }
    int answered = 0;
    for (auto& c : clients) {
        net::Endpoint from;
        if (const auto msg = c->receive(from, 100); msg && *msg == "LOGON SUCCESSFUL") answered++;
    }
    CHECK(answered == 10);

    // One per second from the same address.
    net::UdpSocket again;
    REQUIRE(again.bind(0).empty());
    std::this_thread::sleep_for(1100ms);
    again.send(to, "LOGONv15:tester");
    again.send(to, "LOGONv15:tester");
    int successes = 0;
    for (;;) {
        net::Endpoint from;
        const auto msg = again.receive(from, 300);
        if (!msg) break;
        successes += *msg == "LOGON SUCCESSFUL";
    }
    CHECK(successes == 1);
    server.stop();
}

TEST_CASE("broadcast: a port in use is said to be") {
    net::UdpSocket a, b;
    REQUIRE(a.bind(26027).empty());
    CHECK_FALSE(b.bind(26027).empty());

    net::TcpListener c, d;
    REQUIRE(c.listen(26028).empty());
    CHECK_FALSE(d.listen(26028).empty());

    TlcsServer tlcs;
    CHECK(tlcs.start(26027).find("in use") != std::string::npos);
    WebServer web;
    CHECK(web.start(26028).find("in use") != std::string::npos);
}

TEST_CASE("broadcast: the web page and its state") {
    WebServer server;
    const uint16_t port = 26019;
    REQUIRE(server.start(port).empty());
    server.publish(sample());

    const auto get = [&](const std::string& path) {
        const auto fd = net::connectTo(0x7F000001, port);
        if (!fd) return std::string();
        net::TcpConnection c(*fd);
        c.sendAll("GET " + path + " HTTP/1.0\r\nHost: localhost\r\n\r\n");
        return c.readUntil("\n\n\n\n", 1 << 20, 5000);
    };

    const auto page = get("/");
    CHECK(page.rfind("HTTP/1.0 200", 0) == 0);
    CHECK(page.find("state.json") != std::string::npos);

    const auto state = get("/state.json");
    CHECK(state.rfind("HTTP/1.0 200", 0) == 0);
    CHECK(state.find("\"Test broadcast\"") != std::string::npos);

    CHECK(get("/nothing").rfind("HTTP/1.0 404", 0) == 0);

    // A client that says nothing holds the others a moment only.
    const auto silent = net::connectTo(0x7F000001, port);
    REQUIRE(silent);
    auto start = std::chrono::steady_clock::now();
    CHECK(get("/").rfind("HTTP/1.0 200", 0) == 0);
    CHECK(std::chrono::steady_clock::now() - start < 4s);

    // One that never reads a large answer, nor the stop.
    auto big = sample();
    big.name = std::string(32 << 20, 'x');
    server.publish(big);
    const auto reader = net::connectTo(0x7F000001, port);
    REQUIRE(reader);
    net::TcpConnection c(*reader);
    c.sendAll("GET /state.json HTTP/1.0\r\n\r\n");
    std::this_thread::sleep_for(500ms);
    start = std::chrono::steady_clock::now();
    server.stop();
    CHECK(std::chrono::steady_clock::now() - start < 1s);
    {
        net::TcpConnection close(*silent);
    }
}

TEST_CASE("broadcast: the live files, no temporary file left") {
    const auto dir = std::filesystem::temp_directory_path() / "fcg-live-files";
    std::filesystem::remove_all(dir);
    REQUIRE(writeBroadcastFiles(sample(), dir).empty());
    CHECK(std::filesystem::exists(dir / "live.pgn"));
    CHECK_FALSE(std::filesystem::exists(dir / "live.pgn.tmp"));
    const auto read = [&] {
        std::ifstream in(dir / "live.pgn", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    CHECK(read().find("[Event \"Test broadcast\"]") != std::string::npos);

#ifdef _WIN32
    // A reader that lets others write but not replace the file: written over in place.
    auto replaced           = sample();
    replaced.games[0].white = "Replaced";
    HANDLE reader = CreateFileW((dir / "live.pgn").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    REQUIRE(reader != INVALID_HANDLE_VALUE);
    CHECK(writeBroadcastFiles(replaced, dir).empty());
    CloseHandle(reader);
    CHECK(read().find("[White \"Replaced\"]") != std::string::npos);

    // One that shares nothing: an error, and still no temporary file.
    reader = CreateFileW((dir / "live.pgn").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    REQUIRE(reader != INVALID_HANDLE_VALUE);
    CHECK_FALSE(writeBroadcastFiles(sample(), dir).empty());
    CloseHandle(reader);
    CHECK_FALSE(std::filesystem::exists(dir / "live.pgn.tmp"));
#endif
    std::filesystem::remove_all(dir);
}
