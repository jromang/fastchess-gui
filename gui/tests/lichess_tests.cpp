// lichess broadcasts: the PGN lichess gets, and the relay against a server playing lichess' part, through curl.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include <json.hpp>

#include <fmt/include/fmt/format.h>

#include "https.hpp"
#include "lichess.hpp"
#include "net.hpp"
#include "storage.hpp"

using namespace fcg;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

// Answers every request with `answer(path, body)`, and keeps the requests.
class FakeServer {
   public:
    struct Request {
        std::string path, authorization, content_type, body;
    };

    explicit FakeServer(uint16_t port, std::function<std::string(const Request&)> answer) : answer_(std::move(answer)) {
        ok_     = listener_.listen(port).empty();
        thread_ = std::thread([this] {
            while (!stop_) {
                const auto fd = listener_.accept(100);
                if (!fd) continue;
                net::TcpConnection c(*fd);
                auto text       = c.readUntil("\r\n\r\n", 1 << 20, 2000);
                const auto head = text.find("\r\n\r\n");
                if (head == std::string::npos) continue;
                Request r;
                const auto header = [&](const std::string& name) -> std::string {
                    auto at = text.find("\r\n" + name + ": ");
                    if (at == std::string::npos || at > head) return {};
                    at += name.size() + 4;
                    return text.substr(at, text.find("\r\n", at) - at);
                };
                r.path = text.substr(text.find(' ') + 1, text.find(' ', text.find(' ') + 1) - text.find(' ') - 1);
                r.authorization   = header("Authorization");
                r.content_type    = header("Content-Type");
                const auto length = std::stoul(header("Content-Length").empty() ? "0" : header("Content-Length"));
                r.body            = text.substr(head + 4);
                while (r.body.size() < length) {
                    const auto more = c.readUntil("\x01never", length - r.body.size(), 2000);
                    if (more.empty()) break;
                    r.body += more;
                }
                const auto body = answer_(r);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    requests_.push_back(r);
                }
                c.sendAll("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                          std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
            }
        });
    }
    ~FakeServer() {
        stop_ = true;
        thread_.join();
    }
    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] std::vector<Request> requests() {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests_;
    }

   private:
    std::function<std::string(const Request&)> answer_;
    net::TcpListener listener_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    bool ok_ = false;
    std::mutex mutex_;
    std::vector<Request> requests_;
};

LiveGame liveGame(uint64_t number) {
    LiveGame g;
    g.number    = number;
    g.white     = "Alpha";
    g.black     = "Beta";
    g.start_fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    g.san       = {"e4", "e5", "Nf3"};
    g.clocks    = true;
    g.white_ms  = 59'000;
    g.black_ms  = 3'723'000;
    g.active    = true;
    return g;
}

}  // namespace

TEST_CASE("lichess: the same tags in progress and finished, clocks, the round id of a URL") {
    const auto live = lichessLiveGame(liveGame(3), "Test", 3500, 0, false);
    CHECK(live.key == "2|Alpha|Beta");  // games 3 and 4 are the second encounter
    CHECK_FALSE(live.finished);
    CHECK(live.pgn.find("[Round \"2\"]\n[White \"Alpha\"]\n[Black \"Beta\"]\n[Result \"*\"]\n[WhiteElo \"3500\"]") !=
          std::string::npos);
    CHECK(live.pgn.find("[BlackElo") == std::string::npos);
    CHECK(live.pgn.find("[FEN") == std::string::npos);
    CHECK(live.pgn.find("1. e4 e5 {[%clk 1:02:03]} 2. Nf3 {[%clk 0:00:59]} *") != std::string::npos);

    const std::string pgn =
        "[Event \"x\"]\n[Round \"2\"]\n[White \"Alpha\"]\n[Black \"Beta\"]\n[Result \"1-0\"]\n"
        "[GameDuration \"00:00:10\"]\n\n1. e4 {[%eval 0.30,12] [%clk 0:00:59]} e5 1-0\n\n";
    const auto done = lichessFinishedGame(pgn, "Test", 2, "Alpha", "Beta", 3500, 0, "1-0", false);
    CHECK(done.key == live.key);
    CHECK(done.finished);
    CHECK(done.pgn.rfind("[Event \"Test\"]\n[Site \"fastchess-gui\"]\n[Round \"2\"]", 0) == 0);
    CHECK(done.pgn.find("GameDuration") == std::string::npos);
    CHECK(done.pgn.find("\n\n1. e4 {[%clk 0:00:59]} e5 1-0\n\n") != std::string::npos);

    // Only the clocks of the last move of each side, as while it was played; move numbers after a comment kept.
    const std::string cb =
        "[Event \"x\"]\n\n1. e4 {[%eval 0.30,12] [%clk 0:00:59.120] [%emt 0:00:01.000]} 1... e5 "
        "{[%eval -0.20,11] [%clk 0:00:58.000]} 2. Nf3 {[%clk 0:00:57.500]} 2... Nc6 "
        "{[%clk 0:00:56.900]} 3. Bb5 {[%eval 0.40,14] [%clk 0:00:55.000]} 1/2-1/2\n";
    const auto short_form = lichessFinishedGame(cb, "Test", 1, "Alpha", "Beta", 0, 0, "1/2-1/2", false);
    CHECK(
        short_form.pgn.find("\n\n1. e4 1... e5 2. Nf3 2... Nc6 {[%clk 0:00:56]} 3. Bb5 {[%clk 0:00:55]} 1/2-1/2\n\n") !=
        std::string::npos);
    CHECK(short_form.pgn.find("%eval") == std::string::npos);

    CHECK(lichessRoundId("https://lichess.org/broadcast/my-event/round-1/AbCd1234") == "AbCd1234");
    CHECK(lichessRoundId("AbCd1234/") == "AbCd1234");
    CHECK_FALSE(lichessRoundId("https://lichess.org/broadcast/my-event"));
    CHECK(formEncode({{"name", "A & B"}, {"x", "é"}}) == "name=A+%26+B&x=%C3%A9");
}

TEST_CASE("lichess: a broadcast made, its rounds filled up to 64 games, through curl") {
    if (!curlAvailable()) {
        MESSAGE("curl not available, skipped");
        return;
    }
    const uint16_t port = 26031;
    std::atomic<int> rounds{0};
    FakeServer server(port, [&](const FakeServer::Request& r) -> std::string {
        if (r.path == "/broadcast/new") return R"({"tour":{"id":"TOUR0001"},"rounds":[]})";
        if (r.path == "/broadcast/TOUR0001/new") {
            const auto id = "ROUND00" + std::to_string(++rounds);
            return nlohmann::json{{"round", {{"id", id}, {"url", "http://lichess/" + id}}}}.dump();
        }
        return R"({"games":[]})";
    });
    REQUIRE(server.ok());

    const auto dir = fs::temp_directory_path() / "fcg-lichess-relay";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // The token goes in a header, never on the command line.
    const auto echo =
        httpPost({fmt::format("http://127.0.0.1:{}/api/echo", port), "secret", "text/plain", "body"}, dir / "tmp");
    CHECK(echo.status == 200);
    CHECK(echo.body.find("games") != std::string::npos);
    auto requests = server.requests();
    REQUIRE(requests.size() == 1);
    CHECK(requests[0].authorization == "Bearer secret");
    CHECK(requests[0].content_type == "text/plain");
    CHECK(requests[0].body == "body");
    CHECK(fs::is_empty(dir / "tmp"));  // the config and the body are gone

    LichessRelay relay;
    relay.start({"secret", "", false, "Relay test", dir, dir / "tmp", fmt::format("http://127.0.0.1:{}", port)});
    std::vector<LichessGame> finished;
    for (uint32_t round = 1; round <= 65; ++round) {
        finished.push_back(lichessFinishedGame("[Event \"x\"]\n\n1. e4 1-0\n", "Relay test", round, "Alpha", "Beta", 0,
                                               0, "1-0", false));
    }
    relay.offer({lichessLiveGame(liveGame(131), "Relay test", 0, 0, false)}, finished);
    relay.stop();  // the last push, then stop

    requests = server.requests();
    std::map<std::string, std::string> pushes;  // path -> body
    for (const auto& r : requests) {
        if (r.path.rfind("/api/broadcast/round/", 0) == 0) pushes[r.path] += r.body;
    }
    // A broadcast, its first round full with 64 games, the 65th and the game in progress in the second.
    CHECK(rounds == 2);
    REQUIRE(pushes.size() == 2);
    const auto count = [](const std::string& s) {
        std::size_t n = 0;
        for (auto at = s.find("[Event "); at != std::string::npos; at = s.find("[Event ", at + 1)) ++n;
        return n;
    };
    CHECK(count(pushes["/api/broadcast/round/ROUND001/push"]) == 64);
    CHECK(count(pushes["/api/broadcast/round/ROUND002/push"]) == 2);
    const auto create =
        std::find_if(requests.begin(), requests.end(), [](const auto& r) { return r.path == "/broadcast/new"; });
    REQUIRE(create != requests.end());
    CHECK(create->body.find("name=Relay+test") != std::string::npos);
    CHECK(create->body.find("visibility=unlisted") != std::string::npos);

    // lichess.json: where the games went, which are finished; no token.
    const auto saved = readJson<nlohmann::json>(dir / "lichess.json");
    REQUIRE(saved);
    CHECK((*saved)["tour_id"] == "TOUR0001");
    CHECK((*saved)["round_id"] == "ROUND002");
    CHECK((*saved)["finished"].size() == 65);
    CHECK(saved->dump().find("secret") == std::string::npos);
    CHECK(relay.status().error.empty());

    // Started again on the same tournament: the broadcast is kept, the finished games are not pushed again.
    const auto before = server.requests().size();
    relay.start({"secret", "", false, "Relay test", dir, dir / "tmp", fmt::format("http://127.0.0.1:{}", port)});
    relay.offer({}, finished);
    relay.stop();
    CHECK(server.requests().size() == before);
    fs::remove_all(dir);
}
