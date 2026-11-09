// Broadcast outputs: JSON, live PGN, the TLCS crosstable layout, the TLCS server and the web page.

#include <doctest/doctest.hpp>

#include <chrono>
#include <thread>

#include <json.hpp>

#include "broadcast.hpp"
#include "net.hpp"

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
    s.table  = fcg::tour::computeTable(spec, results);
    s.recent = {"Calpha - Alpha 0-1"};
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
    CHECK(pgn.find("1. e4 e5 2. Nf3 *") != std::string::npos);
    CHECK(pgn.find("[FEN") == std::string::npos);  // the initial position needs none
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
    CHECK(lines.back() == "total games = 3");
}

TEST_CASE("broadcast: a TLCS client logs on and receives the game, the crosstable, and answers to PING") {
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
            if (!pinged) client.send(to, "PING"), pinged = true;
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
    CHECK(has("SITE: Test broadcast"));
    CHECK(has("WPLAYER: Alpha"));
    CHECK(has("BPLAYER: Beta"));
    CHECK(has("FEN: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq"));
    CHECK(has("WMOVE: 1. e4"));
    CHECK(has("BMOVE: 1. e5"));
    CHECK(has("WMOVE: 2. Nf3"));
    CHECK(has("BTIME: 5800"));
    CHECK(has("CTRESET"));
    CHECK(has("CT: total games = 3"));
    CHECK(has("PONG"));

    server.stop();
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
        return c.readUntil("\n\n\n\n", 1 << 20, 1000);
    };

    const auto page = get("/");
    CHECK(page.rfind("HTTP/1.0 200", 0) == 0);
    CHECK(page.find("state.json") != std::string::npos);

    const auto state = get("/state.json");
    CHECK(state.rfind("HTTP/1.0 200", 0) == 0);
    CHECK(state.find("\"Test broadcast\"") != std::string::npos);

    CHECK(get("/nothing").rfind("HTTP/1.0 404", 0) == 0);
    server.stop();
}
