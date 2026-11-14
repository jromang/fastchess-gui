// The WinBoard adapter: the translation line by line, then real games through it with a mock WinBoard engine.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <core/config/config.hpp>
#include <engine/uci_engine.hpp>

#include "../../tournament/tests/self_path.hpp"
#include "engines.hpp"
#include "run.hpp"
#include "suite_run.hpp"
#include "wb2uci.hpp"

using namespace fcg;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

struct Wire {
    std::vector<std::string> gui;     // what the adapter told the GUI
    std::vector<std::string> engine;  // what it told the engine
    wb2uci::Adapter adapter;
    wb2uci::Clock::time_point now = wb2uci::Clock::now();

    Wire()
        : adapter([this](const std::string& l) { gui.push_back(l); },
                  [this](const std::string& l) { engine.push_back(l); }, "engine") {
        adapter.start(now);
    }

    void fromGui(const std::string& l) { adapter.fromGui(l, now); }
    void fromEngine(const std::string& l) { adapter.fromEngine(l, now); }
    void wait(std::chrono::milliseconds d) {
        now += d;
        adapter.tick(now);
    }

    [[nodiscard]] bool sentToEngine(const std::string& l) const {
        return std::find(engine.begin(), engine.end(), l) != engine.end();
    }
    [[nodiscard]] bool sentToGui(const std::string& l) const {
        return std::find(gui.begin(), gui.end(), l) != gui.end();
    }
};

fs::path self() { return fcg::test::selfPath(); }

}  // namespace

TEST_CASE("wb2uci: features and options are parsed as WinBoard writes them") {
    const auto f =
        wb2uci::parseFeatures(R"(ping=1 setboard=1 myname="Fairy-Max 5.0b" option="Resign -check 0" done=1)");
    REQUIRE(f.size() == 5);
    CHECK(f[2] == std::pair<std::string, std::string>{"myname", "Fairy-Max 5.0b"});
    CHECK(f[3].second == "Resign -check 0");

    auto o = wb2uci::parseOption("Resign Threshold -spin 800 200 1200");
    REQUIRE(o);
    CHECK(o->name == "Resign Threshold");
    CHECK(o->kind == "-spin");
    CHECK((o->value == "800" && o->min == "200" && o->max == "1200"));

    o = wb2uci::parseOption("Style -combo Solid /// *Normal /// Risky");
    REQUIRE(o);
    CHECK(o->value == "Normal");
    CHECK(o->vars == std::vector<std::string>{"Solid", "Normal", "Risky"});

    o = wb2uci::parseOption("Ini File -file ./fmax.ini");
    REQUIRE(o);
    CHECK((o->name == "Ini File" && o->value == "./fmax.ini"));
    CHECK_FALSE(wb2uci::parseOption("no kind here"));
}

TEST_CASE("wb2uci: uci waits for the features, then lists the engine's options") {
    Wire w;
    CHECK(w.engine == std::vector<std::string>{"xboard", "protover 2"});
    w.fromGui("uci");
    CHECK(w.gui.empty());  // not before the features

    w.fromEngine(R"(feature ping=1 setboard=1 san=1 usermove=1 memory=1 smp=1 myname="Old Timer")");
    w.fromEngine(R"(feature option="Style -combo Solid /// *Normal" option="Contempt -spin 0 -100 100" foo=1)");
    w.fromEngine("feature done=1");
    CHECK(w.sentToEngine("accepted ping"));
    CHECK(w.sentToEngine("rejected foo"));
    CHECK(w.sentToGui("id name Old Timer"));
    CHECK(w.sentToGui("option name Hash type spin default 64 min 1 max 1048576"));
    CHECK(w.sentToGui("option name Threads type spin default 1 min 1 max 1024"));
    CHECK(w.sentToGui("option name Style type combo default Normal var Solid var Normal"));
    CHECK(w.sentToGui("option name Contempt type spin default 0 min -100 max 100"));
    CHECK(w.gui.back() == "uciok");

    w.fromGui("setoption name Hash value 128");
    w.fromGui("setoption name Contempt value 10");
    CHECK(w.sentToEngine("memory 128"));
    CHECK(w.sentToEngine("option Contempt=10"));

    w.fromGui("isready");
    CHECK(w.sentToEngine("ping 1"));
    CHECK_FALSE(w.sentToGui("readyok"));
    w.fromEngine("pong 1");
    CHECK(w.sentToGui("readyok"));
}

TEST_CASE("wb2uci: scores from White's side reversed for Black when asked; reuse=0 told before uciok") {
    Wire w;
    w.fromGui("uci");
    w.fromEngine("feature ping=1 setboard=1 san=1 usermove=1 reuse=0 done=1");
    CHECK(w.sentToGui("option name Scores from White's side type check default false"));
    REQUIRE(w.gui.size() >= 2);
    CHECK(w.gui[w.gui.size() - 2] == wb2uci::NO_REUSE_INFO);
    CHECK(w.gui.back() == "uciok");

    // Black to move: the engine says +50 for White, that is -50 for Black, the side to move.
    w.fromGui("setoption name Scores from White's side value true");
    w.fromGui("position fen rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    w.fromGui("go wtime 60000 btime 60000");
    w.fromEngine("9 50 100 1000 e5");
    CHECK(w.gui.back().find("score cp -50") != std::string::npos);
    w.fromEngine("10 100003 100 1000 e5");  // White mates: Black is mated
    CHECK(w.gui.back().find("score mate -3") != std::string::npos);
    w.fromEngine("move e5");

    // White to move: unchanged.
    w.fromGui("position fen rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");
    w.fromGui("go wtime 60000 btime 60000");
    w.fromEngine("9 40 100 1000 Nf3");
    CHECK(w.gui.back().find("score cp 40") != std::string::npos);

    // Without reuse=0, nothing is told.
    Wire plain;
    plain.fromGui("uci");
    plain.fromEngine("feature done=1");
    CHECK_FALSE(plain.sentToGui(wb2uci::NO_REUSE_INFO));
}

TEST_CASE("wb2uci: a game in protocol 2: setboard, SAN with usermove, clocks, thinking, the move") {
    Wire w;
    w.fromEngine("feature ping=1 setboard=1 san=1 usermove=1 done=1");
    w.fromGui("ucinewgame");
    w.fromGui("position fen rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1 moves e7e5 g1f3");
    CHECK(w.sentToEngine("new"));
    CHECK(w.sentToEngine("setboard rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"));
    CHECK(w.sentToEngine("usermove e5"));
    CHECK(w.sentToEngine("usermove Nf3"));

    w.engine.clear();
    w.fromGui("go wtime 60000 btime 59000 winc 1000 binc 1000");
    // colors=1 (the default): the other colour around the clocks, the engine's own before go, as WinBoard does.
    CHECK(w.engine == std::vector<std::string>{"level 0 0:59 1", "white", "time 5900", "otim 6000", "black", "go"});

    // Thinking: ply score time(cs) nodes pv in SAN with move numbers; then the move in SAN.
    w.fromEngine("9 31 152 123456 2... Nc6 3. Bb5 a6");
    CHECK(w.gui.back() == "info depth 9 score cp 31 time 1520 nodes 123456 nps 81221 pv b8c6 f1b5 a7a6");
    // Selective depth, speed and tablebase hits before the PV; a sign after the depth: time in seconds.
    w.fromEngine("10& 35? 2 200000 14 100000 0 Nc6");
    CHECK(w.gui.back() == "info depth 10 score cp 35 time 2000 nodes 200000 nps 100000 pv b8c6");
    w.fromEngine("11 31996 3 1000 Nc6");
    CHECK(w.gui.back().find("score mate 2") != std::string::npos);
    w.fromEngine("12 100004 300 5000 Nc6");
    CHECK(w.gui.back().find("score mate 4") != std::string::npos);
    w.fromEngine("move Nc6");
    CHECK(w.gui.back() == "bestmove b8c6");

    // The next move: only the opponent's reply is sent, the engine was told force first; no level again.
    w.engine.clear();
    w.fromGui("position fen rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1 moves e7e5 g1f3 b8c6 f1b5");
    w.fromGui("go wtime 59000 btime 58000 winc 1000 binc 1000");
    CHECK(w.engine ==
          std::vector<std::string>{"force", "usermove Bb5", "white", "time 5800", "otim 5900", "black", "go"});

    // stop: move now.
    w.fromGui("stop");
    CHECK(w.engine.back() == "?");
    w.fromEngine("move a6");
    CHECK(w.gui.back() == "bestmove a7a6");
}

TEST_CASE("wb2uci: protocol 1: no features, edit mode, coordinates, the old move format") {
    Wire w;
    w.fromGui("uci");
    w.wait(wb2uci::FEATURE_TIMEOUT);
    CHECK(w.gui.back() == "uciok");

    w.fromGui("isready");
    CHECK(w.gui.back() == "readyok");  // no ping: at once

    w.engine.clear();
    w.fromGui("position fen 4k3/8/8/8/8/8/4P3/4K2R b K - 0 1");
    CHECK(w.engine == std::vector<std::string>{"new", "force", "a2a3", "edit", "#", "Ke1", "Rh1", "Pe2", "c", "Ke8",
                                               ".", "post", "hard", "easy"});
    w.fromGui("go movetime 2000");
    CHECK(w.sentToEngine("st 2"));
    w.fromEngine("1. ... e8d7");
    CHECK(w.gui.back() == "bestmove e8d7");

    // A command it does not know is not a move it rejected.
    w.fromEngine("Illegal move: computer");
    w.engine.clear();
    w.fromGui("position fen 4k3/8/8/8/8/8/4P3/4K2R b K - 0 1 moves e8d7 e1g1");
    CHECK(w.engine.front() == "force");
    CHECK(w.sentToEngine("e1g1"));
}

TEST_CASE("wb2uci: resignation, claims and moves out of turn") {
    Wire w;
    w.fromEngine("feature done=1");
    w.fromGui("position startpos moves e2e4");
    w.fromGui("go wtime 1000 btime 1000");
    w.fromEngine("1/2-1/2 {Draw by repetition}");  // the GUI judges the game itself
    CHECK(w.gui.back().rfind("info string", 0) == 0);
    w.fromEngine("1-0 {Black resigns}");  // Black, to move, gives the game to White
    CHECK(w.gui.back() == "bestmove resign");

    // A move while it was not asked for one: ignored, and the engine is set up again at the next position.
    w.fromEngine("move e7e5");
    w.engine.clear();
    w.fromGui("position startpos moves e2e4 e7e5");
    CHECK(w.engine.front() == "new");
}

TEST_CASE("wb2uci: the move a stopped search still owes does not answer the next one") {
    Wire w;
    w.fromGui("uci");
    w.wait(wb2uci::FEATURE_TIMEOUT);  // protocol 1: no ping
    w.fromGui("position startpos moves e2e4");
    w.fromGui("go wtime 1000 btime 1000");
    w.fromGui("stop");
    CHECK(w.engine.back() == "?");

    // The GUI moves on (a time loss) before the engine answers: a new game.
    w.fromGui("ucinewgame");
    w.fromGui("position startpos");
    w.gui.clear();
    w.fromGui("isready");
    CHECK(w.gui.empty());  // waits for the owed move
    w.fromEngine("move e7e5");
    CHECK(w.gui == std::vector<std::string>{"readyok"});  // dropped, not a bestmove

    w.fromGui("go wtime 1000 btime 1000");
    w.fromEngine("move e2e4");
    CHECK(w.gui.back() == "bestmove e2e4");

    // An engine that dropped the search silently: ready after a while all the same.
    w.fromGui("stop");
    w.fromGui("position startpos moves e2e4");
    w.gui.clear();
    w.fromGui("isready");
    w.wait(wb2uci::OWED_MOVE_TIMEOUT);
    CHECK(w.gui == std::vector<std::string>{"readyok"});
}

TEST_CASE("wb2uci: a rejected move sets the engine up again; resignation on the opponent's move; claims") {
    Wire w;
    w.fromEngine("feature ping=1 setboard=1 done=1");
    w.fromGui("position startpos moves e2e4");
    w.fromEngine("Illegal move (no matching move)e2e4");
    w.engine.clear();
    w.fromGui("go wtime 1000 btime 1000");
    CHECK(w.engine.front() == "new");
    CHECK(w.sentToEngine("e2e4"));
    w.fromEngine("move e7e5");
    CHECK(w.gui.back() == "bestmove e7e5");

    // The engine pings: it gets its pong.
    w.fromEngine("ping 7");
    CHECK(w.engine.back() == "pong 7");

    // Black resigns when White's move comes in, not during a search: the next go is answered with it.
    w.fromGui("position startpos moves e2e4 e7e5 g1f3");
    w.fromEngine("Black resigns");
    w.fromGui("go wtime 1000 btime 1000");
    CHECK(w.gui.back() == "bestmove resign");

    // A draw claim and then nothing: a forfeit after a while, as xboard forfeits a claim it cannot confirm.
    w.fromGui("ucinewgame");
    w.fromGui("position startpos moves e2e4");
    w.fromGui("go wtime 1000 btime 1000");
    w.fromEngine("1/2-1/2 {Draw by repetition}");
    w.wait(wb2uci::CLAIM_TIMEOUT);
    CHECK(w.gui.back() == "bestmove 0000");
}

TEST_CASE("wb2uci: tournaments of WinBoard engines (protocol 1 and 2) and a UCI engine, through the adapter") {
    const auto wb  = (self().parent_path() / FCG_WB_MOVER).string();
    const auto uci = (self().parent_path() / FCG_RANDOM_MOVER).string();
    if (!fs::is_regular_file(wb) || !fs::is_regular_file(uci)) {
        MESSAGE("mock engines not available on this platform, skipped");
        return;
    }
    fastchess::engine::xboard_adapter = self().string();

    const auto root = fs::temp_directory_path() / "fcg-wb2uci-tests";
    fs::remove_all(root);
    fs::create_directories(root);

    // Openings as positions, Black to move in some: setboard for one engine, edit mode for the other.
    const auto book = root / "openings.epd";
    std::ofstream(book) << "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1\n"
                           "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2\n"
                           "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3\n";

    std::vector<EngineEntry> engines(4);
    engines[0].name = "wb-v2", engines[0].cmd = wb, engines[0].args = "v2", engines[0].protocol = "xboard";
    engines[1].name = "wb-v1", engines[1].cmd = wb, engines[1].args = "v1", engines[1].protocol = "xboard";
    engines[2].name = "wb-resigner", engines[2].cmd = wb, engines[2].args = "v2 resign", engines[2].protocol = "xboard";
    engines[3].name = "uci", engines[3].cmd = uci;

    MatchSetup setup;
    setup.format           = TournamentFormat::RoundRobin;
    setup.name             = "WinBoard";
    setup.participants     = {{"wb-v2"}, {"wb-v1"}, {"wb-resigner"}, {"uci"}};
    setup.cycles           = 1;
    setup.limit            = LimitKind::Time;
    setup.tc_base_s        = 10;
    setup.tc_inc_s         = 0.1;
    setup.book             = book.string();
    setup.concurrency_auto = false;
    setup.concurrency      = 4;
    setup.affinity         = false;
    setup.notify           = false;

    RunController run;
    std::vector<GameRecord> games;
    std::vector<fastchess::Stats> stats;
    REQUIRE(run.start(setup, engines, topologyFromCores({{0, 0}, {0, 1}, {0, 2}, {0, 3}}), root).empty());
    const auto end = std::chrono::steady_clock::now() + 120s;
    while (run.state() != RunController::State::Finished && std::chrono::steady_clock::now() < end) {
        run.update();
        if (auto* obs = run.observer()) obs->drain(games, stats);
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(run.state() == RunController::State::Finished);
    CHECK(run.error().empty());

    const auto& results = run.tournament()->results();
    CHECK(results.size() == 12);  // 6 pairings x 2 games
    int resigned = 0;
    for (const auto& r : results) {
        INFO(r.reason);
        // Games end by the rules or by resignation, never by a protocol failure.
        CHECK(r.reason.find("illegal") == std::string::npos);
        CHECK(r.reason.find("disconnect") == std::string::npos);
        CHECK(r.reason.find("stalls") == std::string::npos);
        CHECK(r.reason.find("on time") == std::string::npos);
        if (r.reason.find("resigns") != std::string::npos) ++resigned;
    }
    CHECK(resigned >= 1);
    fastchess::engine::xboard_adapter.clear();
}

TEST_CASE("wb2uci: what the engine writes after its move, in the same write, reaches the GUI in order") {
    const auto wb = (self().parent_path() / FCG_WB_MOVER).string();
    if (!fs::is_regular_file(wb)) {
        MESSAGE("mock engine not available on this platform, skipped");
        return;
    }
    // The adapter as fastchess-gui starts it, this program in its --wb2uci mode, with an engine that answers a ping
    // only with its next move: its thinking, its move, a telluser and its pong in one write.
    using fastchess::engine::process::Status;
    fastchess::atomic::stop = false;
    fastchess::engine::process::Process process;
    REQUIRE(
        process.init(fs::path(wb).parent_path().string(), self().string(), "--wb2uci \"" + wb + "\" burst", "wb2uci")
            .code == Status::OK);
    process.setupRead();
    std::vector<fastchess::engine::process::Line> lines;
    (void)process.writeInput("uci\n");
    REQUIRE(process.readOutput(lines, "uciok", 5000ms).code == Status::OK);

    // The isready's ping is answered after the move.
    (void)process.writeInput("isready\nucinewgame\nposition startpos\ngo wtime 10000 btime 10000\n");
    REQUIRE(process.readOutput(lines, "readyok", 5000ms).code == Status::OK);
    std::vector<std::string> out;
    std::string read;
    for (const auto& l : lines) out.push_back(l.line), read += l.line + " | ";
    INFO(read);
    const auto at = [&](const std::string& prefix) {
        return std::find_if(out.begin(), out.end(), [&](const auto& l) { return l.rfind(prefix, 0) == 0; }) -
               out.begin();
    };
    REQUIRE(at("bestmove ") < static_cast<long>(out.size()));
    CHECK(at("bestmove ") < at("info string telluser after the move"));
    CHECK(at("info string telluser after the move") < at("readyok"));
    (void)process.writeInput("quit\n");
}

TEST_CASE("wb2uci: an engine added without a protocol is found out to be a WinBoard engine, and read") {
    const auto wb  = (self().parent_path() / FCG_WB_MOVER).string();
    const auto uci = (self().parent_path() / FCG_RANDOM_MOVER).string();
    if (!fs::is_regular_file(wb) || !fs::is_regular_file(uci)) {
        MESSAGE("mock engines not available on this platform, skipped");
        return;
    }
    fastchess::engine::xboard_adapter = self().string();

    EngineEntry e;
    e.name = "wb", e.cmd = wb, e.args = "v2";
    CHECK(detectProtocol(e) == "xboard");
    e.args = "v1";  // no features, but it complains about "uci"
    CHECK(detectProtocol(e) == "xboard");

    // Probes read their timeouts from fastchess' configuration, set by main() in the program.
    if (!fastchess::config::TournamentConfig) {
        fastchess::config::TournamentConfig               = std::make_unique<fastchess::config::Tournament>();
        fastchess::config::TournamentConfig->startup_time = ENGINE_STARTUP_TIME;
    }
    e.args            = "v2";
    const auto result = probeEngine(e);
    REQUIRE(result.ok);
    CHECK(result.protocol == "xboard");
    CHECK(result.id_name == "WB Mover");
    const auto has = [&](const std::string& name) {
        return std::any_of(result.options.begin(), result.options.end(), [&](const auto& o) { return o.name == name; });
    };
    CHECK(has("Hash"));  // feature memory=1
    CHECK(has("Style"));

    EngineEntry u;
    u.name = "uci", u.cmd = uci;
    CHECK(detectProtocol(u) == "uci");
    fastchess::engine::xboard_adapter.clear();
}

TEST_CASE("wb2uci: Chess960: variant fischerandom, X-FEN, castling as O-O to the engine and king takes rook back") {
    Wire w;
    w.fromEngine(R"(feature ping=1 setboard=1 usermove=1 variants="normal,fischerandom" done=1)");
    w.fromGui("uci");
    CHECK(w.sentToGui("option name UCI_Chess960 type check default false"));
    w.fromGui("setoption name UCI_Chess960 value true");

    // White: king f1, rooks e1 and h1; Black: king f8, rooks e8 and g8 (Shredder-FEN HEge, as fastchess writes it):
    // X-FEN KQkq for the engine.
    w.engine.clear();
    w.fromGui("position fen bqnbrkrn/pppppppp/8/8/8/8/PPPPPPPP/BQNBRK1R w HEge - 0 1 moves f1h1");
    CHECK(w.engine[0] == "new");
    CHECK(w.engine[1] == "variant fischerandom");
    CHECK(w.sentToEngine("setboard bqnbrkrn/pppppppp/8/8/8/8/PPPPPPPP/BQNBRK1R w KQkq - 0 1"));
    CHECK(w.sentToEngine("usermove O-O"));  // f1h1: king takes rook

    // Black castles: the engine writes O-O, or the king's move; fastchess gets king takes rook.
    w.fromGui("go wtime 1000 btime 1000");
    w.fromEngine("move O-O");
    CHECK(w.gui.back() == "bestmove f8g8");

    // A new game without Chess960 sends no variant.
    w.fromGui("setoption name UCI_Chess960 value false");
    w.engine.clear();
    w.fromGui("position startpos");
    CHECK_FALSE(w.sentToEngine("variant fischerandom"));
}

TEST_CASE("wb2uci: a Chess960 tournament of WinBoard engines through the adapter") {
    const auto wb = (self().parent_path() / FCG_WB_MOVER).string();
    if (!fs::is_regular_file(wb)) {
        MESSAGE("mock engine not available on this platform, skipped");
        return;
    }
    fastchess::engine::xboard_adapter = self().string();

    const auto root = fs::temp_directory_path() / "fcg-wb2uci-960" / "run";
    fs::remove_all(root.parent_path());
    fs::create_directories(root.parent_path());

    std::vector<EngineEntry> engines(3);
    for (int i = 0; i < 3; ++i) {
        engines[i].name     = "wb" + std::to_string(i);
        engines[i].cmd      = wb;
        engines[i].args     = "v2";
        engines[i].protocol = "xboard";
    }

    MatchSetup setup;
    setup.format           = TournamentFormat::RoundRobin;
    setup.name             = "Chess960";
    setup.participants     = {{"wb0"}, {"wb1"}, {"wb2"}};
    setup.cycles           = 2;
    setup.limit            = LimitKind::Time;
    setup.tc_base_s        = 10;
    setup.tc_inc_s         = 0.1;
    setup.chess960         = true;  // no book: the 960 starting positions
    setup.concurrency_auto = false;
    setup.concurrency      = 3;
    setup.affinity         = false;
    setup.notify           = false;

    RunController run;
    std::vector<GameRecord> games;
    std::vector<fastchess::Stats> stats;
    REQUIRE(run.start(setup, engines, topologyFromCores({{0, 0}, {0, 1}, {0, 2}, {0, 3}}), root).empty());
    const auto end = std::chrono::steady_clock::now() + 120s;
    while (run.state() != RunController::State::Finished && std::chrono::steady_clock::now() < end) {
        run.update();
        if (auto* obs = run.observer()) obs->drain(games, stats);
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(run.state() == RunController::State::Finished);
    CHECK(run.error().empty());
    CHECK(fs::is_regular_file(root / "chess960.epd"));

    const auto& results = run.tournament()->results();
    CHECK(results.size() == 12);
    for (const auto& r : results) {
        INFO(r.reason);
        CHECK(r.reason.find("illegal") == std::string::npos);
        CHECK(r.reason.find("disconnect") == std::string::npos);
        CHECK(r.reason.find("on time") == std::string::npos);
    }

    // The games are Chess960 games in the PGN, and castling happened in some of them.
    std::ifstream pgn_file(run.tournament()->pgnPath());
    const std::string pgn((std::istreambuf_iterator<char>(pgn_file)), std::istreambuf_iterator<char>());
    CHECK(pgn.find("[Variant \"Chess960\"]") != std::string::npos);
    CHECK(pgn.find("O-O") != std::string::npos);
    fastchess::engine::xboard_adapter.clear();
}

TEST_CASE("wb2uci: a test suite with WinBoard engines (protocol 1 and 2), White and Black to move") {
    const auto wb = (self().parent_path() / FCG_WB_MOVER).string();
    if (!fs::is_regular_file(wb)) {
        MESSAGE("mock engines not available on this platform, skipped");
        return;
    }
    fastchess::engine::xboard_adapter = self().string();
    if (!fastchess::config::TournamentConfig)
        fastchess::config::TournamentConfig = std::make_unique<fastchess::config::Tournament>();

    // One legal move each: whatever the mover picks, it finds it.
    tour::Suite suite;
    suite.positions.resize(2);
    REQUIRE(tour::parseSuiteLine("k7/8/8/8/8/8/1q6/K7 w - - bm Kxb2; id \"white\";", suite.positions[0]));
    REQUIRE(tour::parseSuiteLine("K7/8/8/8/8/8/1Q6/k7 b - - bm Kxb2; id \"black\";", suite.positions[1]));

    std::vector<fastchess::EngineConfiguration> engines(2);
    for (int i = 0; i < 2; ++i) {
        engines[i].name     = i == 0 ? "wb-v2" : "wb-v1";
        engines[i].cmd      = wb;
        engines[i].args     = i == 0 ? "v2" : "v1";
        engines[i].protocol = "xboard";
    }
    SuiteSetup setup;
    setup.move_s = 0.2;
    SuiteRunner runner;
    runner.start(suite, engines, setup, 2);
    const auto deadline = std::chrono::steady_clock::now() + 60s;
    while (runner.running() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(20ms);
    REQUIRE_FALSE(runner.running());
    for (const auto& c : runner.columns()) {
        for (std::size_t i = 0; i < 2; ++i) {
            INFO(c.engine << " on " << suite.positions[i].id);
            REQUIRE(c.answers[i]);
            CHECK(c.answers[i]->solved);
            CHECK(c.answers[i]->move == "Kxb2");
        }
    }
}
