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
#include "broadcaster.hpp"
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

TEST_CASE("broadcast: TLCS with lanes serves each concurrent game on its own port, the crosstable after every game") {
    TlcsServer server;
    const uint16_t port = 26031;
    REQUIRE(server.start(port, true).empty());
    CHECK(server.ports() == std::vector<uint16_t>{port});

    // Two games at once: slot 0 on the first port, slot 1 on the next one, opened when its game comes.
    auto s              = sample();
    auto other          = s.games[0];
    other.slot          = 1;
    other.number        = 5;
    other.white         = "Gamma";
    other.black         = "Delta";
    other.san           = {"d4"};
    other.uci           = {"d2d4"};
    other.white_to_move = false;
    s.games.push_back(other);
    server.publish(s, 1);  // a featured slot means nothing to lanes
    std::this_thread::sleep_for(300ms);
    CHECK(server.ports() == std::vector<uint16_t>{port, static_cast<uint16_t>(port + 1)});
    CHECK(server.laneError().empty());

    struct Viewer {
        net::UdpSocket socket;
        net::Endpoint to;
        std::vector<std::string> received;
        std::size_t raw = 0;  // datagrams, before any is acknowledged
        bool acks       = true;
        void listen(std::chrono::milliseconds how_long) {
            const auto until = std::chrono::steady_clock::now() + how_long;
            while (std::chrono::steady_clock::now() < until) {
                net::Endpoint from;
                const auto msg = socket.receive(from, 50);
                if (!msg) continue;
                raw++;
                if (msg->front() == '<') {
                    const auto close = msg->find('>');
                    if (acks) socket.send(to, "ACK: " + msg->substr(1, close - 1));
                    received.push_back(msg->substr(close + 1));
                } else {
                    received.push_back(*msg);
                }
            }
        }
        [[nodiscard]] bool has(const std::string& text) const {
            return std::find(received.begin(), received.end(), text) != received.end();
        }
    };
    Viewer first, second, silent;
    REQUIRE(first.socket.bind(26033).empty());
    REQUIRE(second.socket.bind(26034).empty());
    REQUIRE(silent.socket.bind(26035).empty());
    first.to  = {0x7F000001, port};
    second.to = silent.to = {0x7F000001, static_cast<uint16_t>(port + 1)};
    silent.acks           = false;
    first.socket.send(first.to, "LOGONv15:first");
    second.socket.send(second.to, "LOGONv15:second");
    silent.socket.send(silent.to, "LOGONv15:silent");
    for (int i = 0; i < 6; ++i) {
        first.listen(100ms);
        second.listen(100ms);
        silent.listen(100ms);
    }

    CHECK(first.has("WPLAYER: Alpha"));
    CHECK_FALSE(first.has("WPLAYER: Gamma"));
    CHECK(second.has("WPLAYER: Gamma"));
    CHECK(second.has("BPLAYER: Delta"));
    CHECK(second.has("WMOVE: 1. d4"));
    CHECK_FALSE(second.has("WPLAYER: Alpha"));
    CHECK(first.has("CT: total games = 3"));
    CHECK(second.has("CT: total games = 3"));
    CHECK(server.clients() == 2);
    // The next port keeps the handshake: a client that does not prove its address gets LOGON SUCCESSFUL and the
    // first command (sent again for its ACK), nothing of the game.
    CHECK(silent.has("LOGON SUCCESSFUL"));
    CHECK_FALSE(silent.has("WPLAYER: Gamma"));
    CHECK_FALSE(silent.has("CTRESET"));

    // Game 5 ends: its port tells the result, and both send the crosstable again.
    first.received.clear();
    second.received.clear();
    auto after            = s;
    after.games[1].active = false;
    after.games[1].result = "0-1";
    after.played          = 4;
    server.publish(after, -1);
    for (int i = 0; i < 6; ++i) {
        first.listen(100ms);
        second.listen(100ms);
    }
    CHECK(second.has("result: 0-1"));
    CHECK_FALSE(first.has("result: 0-1"));
    CHECK(first.has("CTRESET"));
    CHECK(second.has("CTRESET"));
    CHECK(first.has("CT: total games = 4"));
    CHECK(second.has("CT: total games = 4"));

    server.stop();
    CHECK(server.ports().empty());
}

TEST_CASE("broadcast: TLCS lanes say when the port of a game is in use") {
    net::UdpSocket taken;
    REQUIRE(taken.bind(26038).empty());
    TlcsServer server;
    REQUIRE(server.start(26037, true).empty());
    auto s     = sample();
    auto other = s.games[0];
    other.slot = 1;
    s.games.push_back(other);
    server.publish(s, -1);
    CHECK(server.ports() == std::vector<uint16_t>{26037});
    CHECK(server.laneError().find("in use") != std::string::npos);
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

TEST_CASE("broadcast: the statistics, the anomalies and each move's search, for the web page") {
    BroadcastInput in;
    in.dir           = "/tournaments/Big one";
    in.spec.format   = fcg::tour::Format::RoundRobin;
    in.spec.pgn_file = "Big one.pgn";
    for (const auto* name : {"Alpha", "Beta", "Gamma"}) {
        fcg::tour::Participant p;
        p.id          = static_cast<int>(in.spec.participants.size());
        p.engine.name = name;
        in.spec.participants.push_back(p);
    }
    const auto result = [](int white, int black, const char* score, fastchess::MatchTermination how, uint64_t offset) {
        fcg::tour::GameResult r;
        r.white       = white;
        r.black       = black;
        r.result      = score;
        r.termination = how;
        r.plies       = 80;
        r.duration_ms = 60000;
        r.pgn         = {offset, 1000};
        return r;
    };
    using fastchess::MatchTermination;
    in.results              = {result(0, 1, "1-0", MatchTermination::NORMAL, 0),
                               result(1, 2, "0-1", MatchTermination::TIMEOUT, 1000),
                               result(2, 0, "1-0", MatchTermination::DISCONNECT, 2000),
                               result(0, 2, "1/2-1/2", MatchTermination::ADJUDICATION, 3000),
                               result(1, 0, "0-1", MatchTermination::ILLEGAL_MOVE, 4000)};
    in.results[2].encounter = 1;
    in.results[2].reason    = "Gamma's connection lost";
    // A voided game is not counted, but its PGN is in the file.
    in.results.push_back(result(2, 1, "0-1", MatchTermination::STALL, 5000));
    in.results.back().voided = true;

    // A game in progress: a book move, then two searched ones.
    SlotState st;
    st.has_game  = true;
    st.active    = true;
    st.game_id   = 7;
    st.white     = "Alpha";
    st.black     = "Beta";
    st.start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    chess::Board board;
    for (const auto* uci : {"e2e4", "e7e5", "g1f3"}) {
        LiveMove m;
        m.move  = chess::uci::uciToMove(board, uci);
        m.san   = chess::uci::moveToSan(board, m.move);
        m.mover = board.sideToMove();
        board.makeMove<true>(m.move);
        st.moves.push_back(m);
    }
    st.moves[0].book    = true;
    st.moves[1].score   = fastchess::Score{fastchess::ScoreType::CP, 30};  // Black's point of view
    st.moves[1].depth   = 14;
    st.moves[1].time_ms = 900;
    st.moves[2].score   = fastchess::Score{fastchess::ScoreType::MATE, 3};
    st.moves[2].depth   = 20;
    st.moves[2].time_ms = 1500;
    st.stm              = chess::Color::BLACK;
    in.slots            = {st};
    in.ended.resize(1);

    const auto s = makeSnapshot(in);
    CHECK(s.stats.games == 5);
    CHECK(s.stats.white_wins == 2);
    CHECK(s.stats.black_wins == 2);
    CHECK(s.stats.draws == 1);
    CHECK(s.stats.plies == 400);
    CHECK(s.stats.adjudications == 1);
    CHECK(s.stats.time_losses == 1);
    CHECK(s.stats.crashes == 1);
    CHECK(s.stats.illegal_moves == 1);
    CHECK(s.stats.stalls == 0);
    // Complete up to the end of the last game recorded, voided or not.
    CHECK(s.pgn_bytes == 6000);
    CHECK(s.pgn_file == std::filesystem::path("/tournaments/Big one") / "Big one.pgn");

    const auto json = nlohmann::json::parse(snapshotJson(s));
    CHECK(json["pgn"] == "Big one.pgn");
    CHECK(json.dump().find("/tournaments") == std::string::npos);  // where it is stays private
    CHECK(json["stats"]["time_losses"] == 1);
    REQUIRE(json["anomalies"].size() == 3);  // newest first
    CHECK(json["anomalies"][0]["kind"] == "illegal");
    CHECK(json["anomalies"][0]["engine"] == "Beta");
    CHECK(json["anomalies"][1]["kind"] == "crash");
    CHECK(json["anomalies"][1]["number"] == 3);
    CHECK(json["anomalies"][1]["engine"] == "Alpha");
    CHECK(json["anomalies"][1]["reason"] == "Gamma's connection lost");
    CHECK(json["anomalies"][2]["kind"] == "time");
    CHECK(json["anomalies"][2]["engine"] == "Beta");

    const auto& game = json["games"][0];
    CHECK(game["evals"] == nlohmann::json::parse("[null, -30, 99997]"));  // White's point of view; mate in 3
    CHECK(game["depths"] == nlohmann::json::parse("[null, 14, 20]"));
    CHECK(game["times_ms"] == nlohmann::json::parse("[null, 900, 1500]"));

    // No game recorded yet: nothing to download.
    in.results.clear();
    const auto empty = makeSnapshot(in);
    CHECK(empty.pgn_file.empty());
    CHECK(nlohmann::json::parse(snapshotJson(empty))["pgn"].is_null());
}

TEST_CASE("broadcast: the web page offers the PGN file, the games recorded only, without holding the page") {
    const auto dir = std::filesystem::temp_directory_path() / "fcg-web-pgn";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto file = dir / "Été 2026.pgn";
    // 3 MB of games, then the start of one being written.
    std::string games;
    for (int i = 0; games.size() < (3u << 20); ++i) {
        games += fmt::format("[Event \"Été\"]\n[Round \"{}\"]\n[Result \"1-0\"]\n\n1. e4 e5 2. Nf3 1-0\n\n", i);
    }
    {
        std::ofstream out(file, std::ios::binary);
        out << games << "[Event \"Été\"]\n[Round";
    }

    WebServer server;
    const uint16_t port = 26041;
    REQUIRE(server.start(port).empty());
    auto s = sample();
    server.publish(s);

    const auto get = [&](const std::string& path) {
        const auto fd = net::connectTo(0x7F000001, port);
        if (!fd) return std::string();
        net::TcpConnection c(*fd);
        c.sendAll("GET " + path + " HTTP/1.0\r\nHost: localhost\r\n\r\n");
        return c.readUntil("\n\n\n\n", 8 << 20, 10000);
    };
    // Nothing recorded yet.
    CHECK(get("/games.pgn").rfind("HTTP/1.0 404", 0) == 0);

    s.pgn_file  = file;
    s.pgn_name  = "Été 2026.pgn";
    s.pgn_bytes = games.size();
    server.publish(s);
    CHECK(get("/").find("games.pgn") != std::string::npos);

    const auto answer = get("/games.pgn");
    REQUIRE(answer.rfind("HTTP/1.0 200", 0) == 0);
    const auto body = answer.find("\r\n\r\n");
    REQUIRE(body != std::string::npos);
    const auto head = answer.substr(0, body);
    CHECK(head.find("Content-Type: application/x-chess-pgn") != std::string::npos);
    CHECK(head.find(fmt::format("Content-Length: {}", games.size())) != std::string::npos);
    CHECK(head.find("filename=\"__t__ 2026.pgn\"") != std::string::npos);
    CHECK(head.find("filename*=UTF-8''%C3%89t%C3%A9%202026.pgn") != std::string::npos);
    CHECK(answer.substr(body + 4) == games);  // all of it, the game being written left out

    // Downloads that take nothing hold neither the page nor its state, two at a time; the next is told to wait. The
    // file larger than what the system's buffers take in for them.
    {
        std::ofstream out(file, std::ios::binary | std::ios::app);
        const std::string line(1023, 'x');
        for (int i = 0; i < 48 * 1024; ++i) out << line << '\n';
    }
    s.pgn_bytes = std::filesystem::file_size(file);
    server.publish(s);
    std::vector<std::unique_ptr<net::TcpConnection>> stuck;
    for (int i = 0; i < 2; ++i) {
        const auto fd = net::connectTo(0x7F000001, port);
        REQUIRE(fd);
        stuck.push_back(std::make_unique<net::TcpConnection>(*fd));
        stuck.back()->sendAll("GET /games.pgn HTTP/1.0\r\n\r\n");
    }
    std::this_thread::sleep_for(500ms);
    auto start = std::chrono::steady_clock::now();
    CHECK(get("/state.json").rfind("HTTP/1.0 200", 0) == 0);
    CHECK(get("/").rfind("HTTP/1.0 200", 0) == 0);
    CHECK(std::chrono::steady_clock::now() - start < 2s);
    CHECK(get("/games.pgn").rfind("HTTP/1.0 503", 0) == 0);

    // A stop does not wait for them.
    start = std::chrono::steady_clock::now();
    server.stop();
    CHECK(std::chrono::steady_clock::now() - start < 2s);
    stuck.clear();
    std::filesystem::remove_all(dir);
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
