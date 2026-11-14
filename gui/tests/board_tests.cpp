// The engine client of the console and the Board page, against the mock engine; the analysis; the Board page's
// texts (FEN, PGN, move lists) and the analysis of a game.

#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

#include <engine/uci_engine.hpp>

#include "../../tournament/tests/self_path.hpp"
#include "analysis.hpp"
#include "engine_client.hpp"
#include "run.hpp"

using namespace fcg;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

std::string randomMover() { return (fcg::test::selfPath().parent_path() / FCG_RANDOM_MOVER).string(); }

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

// Sets a variable of the environment for as long as it lives.
struct ScopedEnv {
    const char* name;
    ScopedEnv(const char* n, const char* value) : name(n) { setEnv(n, value); }
    ~ScopedEnv() { setEnv(name, nullptr); }
};

EngineEntry mover() {
    EngineEntry e;
    e.name     = "mover";
    e.cmd      = randomMover();
    e.protocol = "uci";
    return e;
}

// Polls the analyser until `done` or 10 seconds.
template <typename F>
bool pollUntil(Analyser& a, F done) {
    const auto until = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < until) {
        a.poll();
        if (done()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return false;
}

}  // namespace

TEST_CASE("engine client: a process of its own, off fastchess' list, that answers and ends") {
    if (!fs::is_regular_file(randomMover())) return;
    const int before = engineProcesses();

    EngineProcess p;
    std::string error;
    REQUIRE(p.start(engineLaunch(mover()), error));
    CHECK(p.running());
    // A tournament's end ends every process on fastchess' list: this one is not there.
    CHECK(engineProcesses() == before);

    CHECK(p.send("uci"));
    std::vector<EngineProcess::Line> lines;
    const auto until = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < until) {
        p.wait(100ms);
        for (auto& l : p.take()) lines.push_back(std::move(l));
        if (!lines.empty() && lines.back().text == "uciok") break;
    }
    REQUIRE(!lines.empty());
    CHECK(lines.front().kind == EngineProcess::Line::Kind::Sent);
    CHECK(lines.front().text == "uci");
    CHECK(lines.back().text == "uciok");
    CHECK(std::any_of(lines.begin(), lines.end(), [](const auto& l) { return l.text == "id name random_move"; }));

    // The transcript marks what was sent.
    const auto text = consoleTranscript(lines);
    CHECK(text.find(" > uci\n") != std::string::npos);
    CHECK(text.find("   uciok\n") != std::string::npos);

    p.stop();
    CHECK_FALSE(p.running());
    CHECK_FALSE(p.send("isready"));

    // A program that is not there.
    auto missing = mover();
    missing.cmd  = (fs::temp_directory_path() / "no-such-engine-fcg").string();
    CHECK_FALSE(p.start(engineLaunch(missing), error));
    CHECK(error.find("no-such-engine-fcg") != std::string::npos);
}

TEST_CASE("engine client: a WinBoard engine goes through the adapter unless spoken to in its own protocol") {
    const std::string adapter         = fastchess::engine::xboard_adapter;
    fastchess::engine::xboard_adapter = "/opt/fcg/fastchess-gui";
    EngineEntry e;
    e.name = "wb", e.cmd = "/engines/crafty", e.args = "xboard", e.protocol = "xboard", e.dir = "/engines";
    const auto through = engineLaunch(e);
    CHECK(through.program == "/opt/fcg/fastchess-gui");
    CHECK(through.args == "--wb2uci \"/engines/crafty\" xboard");
    CHECK(through.dir == "/engines");
    const auto raw = engineLaunch(e, true);
    CHECK(raw.program == "/engines/crafty");
    CHECK(raw.args == "xboard");
    fastchess::engine::xboard_adapter = adapter;
}

TEST_CASE("engine client: the console's history and the core claims") {
    std::vector<std::string> history;
    rememberCommand(history, "uci");
    rememberCommand(history, "uci");
    rememberCommand(history, "");
    rememberCommand(history, "isready");
    CHECK(history == std::vector<std::string>{"uci", "isready"});
    for (int i = 0; i < 300; ++i) rememberCommand(history, "go depth " + std::to_string(i));
    CHECK(history.size() == 200);
    CHECK(history.back() == "go depth 299");

    const int before = boardThreads();
    {
        CoreClaim a(4);
        CoreClaim b;
        b.set(2);
        CHECK(boardThreads() == before + 6);
        a.set(1);
        CHECK(boardThreads() == before + 3);
    }
    CHECK(boardThreads() == before);
}

TEST_CASE("analysis: info lines into MultiPV lines") {
    std::vector<AnalysisLine> lines;
    CHECK(takeInfoLine(lines,
                       "info depth 12 seldepth 18 multipv 1 score cp 35 wdl 300 600 100 nodes 12345 nps 900000 "
                       "time 14 pv e2e4 e7e5 g1f3",
                       3));
    CHECK(takeInfoLine(lines, "info depth 12 multipv 3 score mate -4 nodes 12400 pv a2a3", 3));
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].depth == 12);
    CHECK(lines[0].seldepth == 18);
    CHECK(lines[0].score->value == 35);
    CHECK(lines[0].wdl == std::array<int64_t, 3>{300, 600, 100});
    CHECK(lines[0].pv == std::vector<std::string>{"e2e4", "e7e5", "g1f3"});
    CHECK(lines[1].pv.empty());  // not told yet
    CHECK(lines[2].score->type == fastchess::ScoreType::MATE);
    CHECK(lines[2].score->value == -4);

    // A bound without its PV keeps the last PV; a rank beyond those asked, a currmove line, a string: nothing.
    CHECK(takeInfoLine(lines, "info depth 13 multipv 1 score cp 50 lowerbound nodes 20000", 3));
    CHECK(lines[0].lowerbound);
    CHECK(lines[0].score->value == 50);
    CHECK(lines[0].pv.size() == 3);
    CHECK_FALSE(takeInfoLine(lines, "info depth 13 multipv 4 score cp 1 pv h2h3", 3));
    CHECK_FALSE(takeInfoLine(lines, "info depth 13 currmove e2e4 currmovenumber 1", 3));
    CHECK_FALSE(takeInfoLine(lines, "info string score cp 10 is a joke", 3));
    CHECK_FALSE(takeInfoLine(lines, "bestmove e2e4", 3));
    CHECK(lines.size() == 3);

    CHECK(goCommand({}) == "go infinite");
    CHECK(goCommand({1500, 0}) == "go movetime 1500");
    CHECK(goCommand({0, 18}) == "go depth 18");
    CHECK(positionCommand({"8/8/8/8/8/8/8/K1k5 w - - 0 1", {}, false}) == "position fen 8/8/8/8/8/8/8/K1k5 w - - 0 1");
    CHECK(positionCommand({std::string(chess::constants::STARTPOS), {"e2e4", "e7e5"}, false}) ==
          "position fen " + std::string(chess::constants::STARTPOS) + " moves e2e4 e7e5");
}

TEST_CASE("analysis: the mock engine analyses positions one after the other, with several lines") {
    if (!fs::is_regular_file(randomMover())) return;
    ScopedEnv env("FCG_MOCK_ANALYSIS", "1");
    const int before = boardThreads();

    Analyser a;
    AnalysisSettings settings;
    settings.engine  = "mover";
    settings.threads = 3;
    settings.multipv = 3;
    a.start(mover(), settings);
    const AnalysisPosition start{std::string(chess::constants::STARTPOS), {}, false};
    a.analyse(start);
    REQUIRE(pollUntil(a, [&] { return a.lines().size() == 3 && a.lines()[0].depth == 8; }));
    CHECK(a.state() == Analyser::State::Searching);
    CHECK(a.hasOption("MultiPV"));
    CHECK(a.lines()[0].wdl.has_value());  // UCI_ShowWDL was set
    CHECK(a.lines()[0].pv.front() == "a2a3");
    CHECK(a.threadsInUse() == 3);
    CHECK(boardThreads() == before + 3);
    CHECK_FALSE(a.finished());

    // Another position: the search is stopped, its move awaited, and the new one searched.
    const AnalysisPosition after{std::string(chess::constants::STARTPOS), {"e2e4"}, false};
    a.analyse(after);
    REQUIRE(pollUntil(a, [&] {
        return a.position() == after && a.lines().size() == 3 && a.lines()[0].depth == 8 &&
               a.state() == Analyser::State::Searching;
    }));
    CHECK(a.lines()[0].pv.front() == "a7a6");  // Black's moves now

    // Fewer lines, sent between two searches: the search starts again with them.
    settings.multipv = 1;
    a.update(settings);
    REQUIRE(pollUntil(a, [&] { return a.state() == Analyser::State::Searching && a.lines().size() == 1; }));

    // A limited search ends with the engine's move.
    a.analyse(start, {200, 0});
    REQUIRE(pollUntil(a, [&] { return a.finished(); }));
    CHECK(a.bestmove() == "a2a3");
    CHECK(a.threadsInUse() == 0);

    a.halt();
    a.stop();
    CHECK(a.state() == Analyser::State::Off);
    CHECK(boardThreads() == before);
}

TEST_CASE("analysis: an engine that cannot start, or stops, says so") {
    Analyser a;
    auto e = mover();
    e.cmd  = (fs::temp_directory_path() / "no-such-engine-fcg").string();
    a.start(e, {});
    CHECK(a.state() == Analyser::State::Failed);
    CHECK(a.error().find("mover") != std::string::npos);
}
