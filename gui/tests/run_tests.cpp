// Plays real tournaments through RunController with fastchess' random mover engine.

#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <set>
#include <thread>

#include <core/config/config.hpp>
#include <core/globals/globals.hpp>
#include <engine/uci_engine.hpp>

#include "engines.hpp"
#include "headless.hpp"
#include "net.hpp"
#include "proc_tree.hpp"
#include "run.hpp"
#include "storage.hpp"
#include "suite_run.hpp"

#ifdef _WIN32
#    include <windows.h>
// After windows.h, which it needs.
#    include <tlhelp32.h>
#else
#    include <unistd.h>
#endif

using namespace fcg;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

// The test binary.
fs::path self() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    const auto n = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return fs::path(std::string(buffer, n));
#else
    return fs::read_symlink("/proc/self/exe");
#endif
}

// The mock engine is built next to the test binary.
std::string randomMover() { return (self().parent_path() / FCG_RANDOM_MOVER).string(); }

// Sets (or, with nullptr, removes) a variable of the environment, which the mock engine reads.
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

struct Harness {
    RunController run;
    std::vector<GameRecord> games;
    std::vector<fastchess::Stats> stats;

    void pump() {
        run.update();
        if (auto* obs = run.observer()) obs->drain(games, stats);
    }

    // Pumps until `done` holds or the timeout expires.
    template <typename F>
    bool waitFor(F done, std::chrono::milliseconds timeout = 20s) {
        const auto end = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < end) {
            pump();
            if (done()) return true;
            std::this_thread::sleep_for(10ms);
        }
        return false;
    }
};

MatchSetup randomSetup(int games) {
    MatchSetup setup;
    setup.engine_a         = "random-a";
    setup.engine_b         = "random-b";
    setup.mode             = MatchMode::Fixed;
    setup.fixed_games      = games;
    setup.limit            = LimitKind::Nodes;
    setup.nodes            = 1;
    setup.concurrency_auto = false;
    setup.concurrency      = 2;
    setup.affinity         = false;
    setup.notify           = false;
    return setup;
}

std::vector<EngineEntry> randomEngines() {
    std::vector<EngineEntry> engines(2);
    engines[0].name = "random-a";
    engines[0].cmd  = randomMover();
    engines[1].name = "random-b";
    engines[1].cmd  = randomMover();
    return engines;
}

}  // namespace

TEST_CASE("pause lets running games finish and starts no new one; stop when idle ends the run") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }

    const auto root = fs::temp_directory_path() / "fcg-run-tests";
    fs::remove_all(root);

    Harness h;
    REQUIRE(h.run.start(randomSetup(400), randomEngines(), topologyFromCores({{0, 0}, {0, 1}, {0, 2}, {0, 3}}), root)
                .empty());
    CHECK(h.run.running());

    REQUIRE(h.waitFor([&] { return h.games.size() >= 4; }));

    h.run.setPaused(true);
    CHECK(h.run.paused());

    // The games that were running finish...
    REQUIRE(h.waitFor([&] { return h.run.runningGames() == 0; }));
    const auto at_pause = h.games.size();

    // ...and nothing starts while paused.
    std::this_thread::sleep_for(700ms);
    h.pump();
    CHECK(h.games.size() == at_pause);
    CHECK(h.run.runningGames() == 0);
    CHECK(h.run.running());

    h.run.setPaused(false);
    REQUIRE(h.waitFor([&] { return h.games.size() >= at_pause + 4; }));

    h.run.stopWhenIdle();
    REQUIRE(h.waitFor([&] { return h.run.state() == RunController::State::Finished; }));
    CHECK(h.run.takeFinishedEvent());
    CHECK(h.run.stoppedByUser());
    CHECK(h.run.error().empty());

    // Every recorded game was scored by fastchess, and nothing was lost when stopping.
    REQUIRE_FALSE(h.stats.empty());
    CHECK(static_cast<std::size_t>(h.stats.back().sum()) == h.games.size());
    CHECK(h.games.size() < 400);

    // Both PGN files hold every game.
    auto* obs = h.run.observer();
    REQUIRE(obs);
    const auto& last = h.games.back();
    CHECK(tour::readSpan(obs->fastchessPgn(), last.fastchess_span).has_value());
    CHECK(tour::readSpan(obs->chessbasePgn(), last.chessbase_span).has_value());

    // A second run in the same process works (fastchess keeps its state in globals).
    Harness second;
    REQUIRE(second.run.start(randomSetup(6), randomEngines(), topologyFromCores({{0, 0}, {0, 1}}), root).empty());
    REQUIRE(second.waitFor([&] { return second.run.state() == RunController::State::Finished; }, 30s));
    CHECK_FALSE(second.run.stoppedByUser());
    CHECK(second.games.size() == 6);
    CHECK(second.stats.back().sum() == 6);
}

TEST_CASE("an engine whose file name has a space is the one started, not the program named like its first word") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }

    if (!fastchess::config::TournamentConfig)
        fastchess::config::TournamentConfig = std::make_unique<fastchess::config::Tournament>();

    // The engine may still be exiting when the directory is removed, so removal errors are ignored.
    std::error_code ec;
    const auto dir = fs::temp_directory_path() / "fcg-probe-tests";
    fs::remove_all(dir, ec);
    fs::create_directories(dir);

    // Started unquoted, "mover 4.4.exe" would run the decoy "mover.exe" with the argument "4.4.exe".
    const auto ext = fs::path(randomMover()).extension().string();
    fs::copy_file(randomMover(), dir / ("mover 4.4" + ext));
    std::ofstream(dir / ("mover" + ext)) << "not a program";

    EngineEntry entry;
    entry.cmd = (dir / ("mover 4.4" + ext)).string();

    const auto result = probeEngine(entry);
    CHECK_MESSAGE(result.ok, result.error);
    CHECK_FALSE(result.options.empty());

    fs::remove_all(dir, ec);
}

TEST_CASE("the engine check runs fastchess' UCI compliance steps, and stops at the first that fails") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }
    if (!fastchess::config::TournamentConfig)
        fastchess::config::TournamentConfig = std::make_unique<fastchess::config::Tournament>();

    EngineEntry entry;
    entry.cmd         = randomMover();
    const auto passed = checkEngine(entry);
    CHECK_MESSAGE(passed.ok, passed.message);
    CHECK(passed.report.rfind("✓ Start the engine\n", 0) == 0);
    CHECK(passed.report.find("✗") == std::string::npos);
    CHECK(passed.message.find("UCI: ") != std::string::npos);

#ifndef _WIN32
    // An engine that plays, but gives no "id author": the fourth step fails, with what it wrote.
    const auto dir = fs::temp_directory_path() / "fcg-check-tests";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto script = dir / "no-author.sh";
    std::ofstream(script) << "#!/bin/sh\n"
                             "while read line; do case \"$line\" in\n"
                             "uci) echo 'id name No author'; echo uciok;;\n"
                             "isready) echo readyok;;\n"
                             "go*) echo 'info depth 1 score cp 0 pv e2e4'; echo 'bestmove e2e4';;\n"
                             "quit) exit 0;;\n"
                             "esac; done\n";
    fs::permissions(script, fs::perms::owner_all);
    entry.cmd         = script.string();
    const auto failed = checkEngine(entry);
    CHECK_FALSE(failed.ok);
    CHECK(failed.message.find("4") != std::string::npos);
    CHECK(failed.report.find("✓ Check id name\n✗ Check id author\n") != std::string::npos);
    CHECK(failed.report.find("id name No author") != std::string::npos);
    fs::remove_all(dir);
#endif
}

#ifndef _WIN32
TEST_CASE("an engine that describes an option badly is read and checked, the option kept and settable") {
    // What Amyan 1.72 and Cerebro 3.03d announce (#45): a spin option without bounds, one whose default is outside
    // its range, a combo whose default is not one of its values. fastchess refused the whole engine for the first
    // two, so no option was shown and no game could start.
    if (!fastchess::config::TournamentConfig)
        fastchess::config::TournamentConfig = std::make_unique<fastchess::config::Tournament>();
    const auto dir = fs::temp_directory_path() / "fcg-bad-options";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "uci.txt") << "id name Cerebro 3.03d\n"
                                      "id author Antonio Senatore\n"
                                      "option name Hash type spin default 256 min 1 max 512\n"
                                      "option name Pawn Hash type spin default 8 min 1 max 512\n"
                                      "option name NalimovCache type spin default 64 min 2 max 16\n"
                                      "option name NalimovPath type string default F:\\Sakk\\TB\n"
                                      "option name MultiPV type spin min 1 max 255 default 1\n"
                                      "option name Noise type spin default 4 min 2 max 6\n"
                                      "option name Book Mode type combo default Tournamen var Tournament var Normal\n"
                                      "option name Ponder type check default false\n"
                                      "option name Clear Hash type button\n"
                                      "option name Amyan MultiPV type spin default 1\n"
                                      "uciok\n";
    // Cerebro's banner, its board and its prompt, without a newline, before it reads anything.
    std::ofstream(dir / "banner.txt") << "by Antonio Senatore, Argentina\n"
                                         "Version 3.03d (Jun-08-08)\n\n"
                                         "Found Cerebro.cfg!\n"
                                         "Hash table memory = 256 Mb\n\n"
                                         "                             8  r n b q k b n r\n"
                                         "                             1  R N B Q K B N R\n\n"
                                         "white(1): ";
    const auto script = dir / "bad-options.sh";
    std::ofstream(script) << "#!/bin/sh\n"
                             "cat \"$(dirname \"$0\")/banner.txt\"\n"
                             "while read line; do case \"$line\" in\n"
                             "uci) echo; cat \"$(dirname \"$0\")/uci.txt\";;\n"
                             "isready) echo readyok;;\n"
                             "go*) echo 'info depth 1 score cp 0 pv e2e4'; echo 'bestmove e2e4';;\n"
                             "quit) exit 0;;\n"
                             "esac; done\n";
    fs::permissions(script, fs::perms::owner_all);

    EngineEntry entry;
    entry.name       = "Cerebro 3.03d";
    entry.cmd        = script.string();
    entry.protocol   = "uci";
    const auto probe = probeEngine(entry);
    REQUIRE_MESSAGE(probe.ok, probe.error);
    std::set<std::string> names;
    for (const auto& o : probe.options) names.insert(o.name);
    CHECK(names == std::set<std::string>{"Hash", "Pawn Hash", "NalimovCache", "NalimovPath", "MultiPV", "Noise",
                                         "Book Mode", "Ponder", "Clear Hash", "Amyan MultiPV"});

    entry.options      = {{"NalimovCache", "16"}, {"Amyan MultiPV", "2"}, {"MultiPV", "3"}};
    const auto checked = checkEngine(entry);
    CHECK_MESSAGE(checked.ok, checked.message);
    fs::remove_all(dir);
}
#endif

#ifndef _WIN32
TEST_CASE("the check takes a move from the engine's own book, without a search line, as compliant") {
    // Amyan 1.72 answers the start position from its book: "info string  e2e4 70%,d2d4 30%", then bestmove (#45).
    if (!fastchess::config::TournamentConfig)
        fastchess::config::TournamentConfig = std::make_unique<fastchess::config::Tournament>();
    const auto dir = fs::temp_directory_path() / "fcg-book-move";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto script = dir / "book-move.sh";
    std::ofstream(script) << "#!/bin/sh\n"
                             "start=0\n"
                             "while read line; do case \"$line\" in\n"
                             "uci) echo 'id name Book mover'; echo 'id author Test'; echo uciok;;\n"
                             "isready) echo readyok;;\n"
                             "'position startpos') start=1;;\n"
                             "position*) start=0;;\n"
                             "'go wtime 100 btime 100') if [ $start = 1 ]; then\n"
                             "    echo 'info string  e2e4 70%,d2d4 30%'; echo 'bestmove d2d4'\n"
                             "  else echo 'info depth 1 score cp 0 pv e2e4'; echo 'bestmove e2e4'; fi;;\n"
                             "go*) echo 'info depth 1 score cp 0 pv e2e4'; echo 'bestmove e2e4';;\n"
                             "quit) exit 0;;\n"
                             "esac; done\n";
    fs::permissions(script, fs::perms::owner_all);

    EngineEntry entry;
    entry.name         = "Book mover";
    entry.cmd          = script.string();
    entry.protocol     = "uci";
    const auto checked = checkEngine(entry);
    CHECK_MESSAGE(checked.ok, checked.message << "\n" << checked.report);
    fs::remove_all(dir);
}

TEST_CASE("the check sends the engine's options, and a failed read or check shows what the engine wrote") {
    if (!fastchess::config::TournamentConfig)
        fastchess::config::TournamentConfig = std::make_unique<fastchess::config::Tournament>();
    const auto dir = fs::temp_directory_path() / "fcg-check-options";
    fs::remove_all(dir);
    fs::create_directories(dir);
    // Like Stockfish with a wrong EvalFile: it stops once the network it was given cannot be loaded.
    const auto script = dir / "evalfile.sh";
    std::ofstream(script)
        << "#!/bin/sh\n"
           "eval=nn.bin\n"
           "while read line; do case \"$line\" in\n"
           "uci) echo 'id name Evalfile'; echo 'id author Test';\n"
           "     echo 'option name EvalFile type string default nn.bin'; echo uciok;;\n"
           "'setoption name EvalFile value '*) eval=${line#setoption name EvalFile value };;\n"
           "isready) if [ \"$eval\" != nn.bin ]; then echo \"info string ERROR: cannot load $eval\";\n"
           "         exit 1; fi; echo readyok;;\n"
           "go*) echo 'info depth 1 score cp 0 pv e2e4'; echo 'bestmove e2e4';;\n"
           "quit) exit 0;;\n"
           "esac; done\n";
    fs::permissions(script, fs::perms::owner_all);

    EngineEntry entry;
    entry.name     = "Evalfile";
    entry.cmd      = script.string();
    entry.protocol = "uci";
    CHECK_MESSAGE(checkEngine(entry).ok, checkEngine(entry).message);

    entry.options    = {{"EvalFile", "missing.bin"}};
    const auto wrong = checkEngine(entry);
    CHECK_FALSE(wrong.ok);
    CHECK(wrong.report.find("ERROR: cannot load missing.bin") != std::string::npos);

    entry.options      = {{"Threads", "4"}};
    const auto unknown = checkEngine(entry);
    CHECK_FALSE(unknown.ok);
    CHECK(unknown.message.find("Threads") != std::string::npos);

    // An engine that cannot start: the read fails with its last words.
    const auto broken = dir / "broken.sh";
    std::ofstream(broken) << "#!/bin/sh\necho 'cannot find MSVCR100.dll'\nexit 1\n";
    fs::permissions(broken, fs::perms::owner_all);
    entry.cmd        = broken.string();
    const auto probe = probeEngine(entry);
    CHECK_FALSE(probe.ok);
    CHECK(probe.output.find("cannot find MSVCR100.dll") != std::string::npos);
    fs::remove_all(dir);
}
#endif

TEST_CASE("a match stopped abruptly is offered for resume and ends with exactly its games") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }

    const auto root = fs::temp_directory_path() / "fcg-run-tests-resume";
    fs::remove_all(root);
    const auto topology = topologyFromCores({{0, 0}, {0, 1}, {0, 2}, {0, 3}});

    std::vector<EngineTotals> totals;
    {
        Harness h;
        REQUIRE(h.run.start(randomSetup(40), randomEngines(), topology, root).empty());
        REQUIRE(h.waitFor([&] { return h.games.size() >= 8; }));
        h.run.stopNow();  // running games are lost, as when the program is killed
        REQUIRE(h.waitFor([&] { return h.run.state() == RunController::State::Finished; }));
        totals = h.run.observer()->totals();
    }

    const auto unfinished = unfinishedMatch(root);
    REQUIRE(unfinished);
    CHECK(unfinished->played >= 8);
    CHECK(unfinished->played < 40);
    REQUIRE(unfinished->total);
    CHECK(*unfinished->total == 40);

    // The search statistics of the games played are read back from the PGN.
    {
        Harness viewer;
        REQUIRE(viewer.run.view(unfinished->dir).empty());
        REQUIRE(viewer.waitFor([&] { return !viewer.run.observer()->loading(); }));
        const auto again = viewer.run.observer()->totals();
        REQUIRE(again.size() == totals.size());
        for (std::size_t i = 0; i < totals.size(); ++i) {
            CHECK(totals[i].moves > 0);
            CHECK(again[i].moves == totals[i].moves);
            CHECK(again[i].depth_sum == totals[i].depth_sum);
            CHECK(again[i].time_ms == totals[i].time_ms);
        }
    }

    Harness h;
    REQUIRE(h.run.resume(unfinished->dir).empty());
    REQUIRE(h.waitFor([&] { return h.run.state() == RunController::State::Finished; }, 30s));
    CHECK(h.run.error().empty());
    CHECK_FALSE(h.run.stoppedByUser());

    // The games played before the stop are shown again, and the match ends with exactly 40.
    CHECK(h.games.size() == 40);
    REQUIRE_FALSE(h.stats.empty());
    CHECK(h.stats.back().sum() == 40);

    std::set<uint64_t> ids;
    for (const auto& g : h.games) ids.insert(g.game_id);
    CHECK(ids.size() == 40);

    CHECK_FALSE(unfinishedMatch(root));
    CHECK_FALSE(h.run.resume(unfinished->dir).empty());  // already finished
}

TEST_CASE("a round robin of three engines plays every pairing and shows every name") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }

    const auto root = fs::temp_directory_path() / "fcg-run-tests-rr";
    fs::remove_all(root);

    auto engines = randomEngines();
    engines.push_back(engines[0]);
    engines[2].name = "random-c";

    MatchSetup setup   = randomSetup(0);
    setup.format       = TournamentFormat::RoundRobin;
    setup.cycles       = 2;
    setup.participants = {{"random-a"}, {"random-b"}, {"random-c"}};
    setup.name         = "Three randoms";

    Harness h;
    REQUIRE(h.run.start(setup, engines, topologyFromCores({{0, 0}, {0, 1}, {0, 2}, {0, 3}}), root).empty());
    REQUIRE(h.waitFor([&] { return h.run.state() == RunController::State::Finished; }, 30s));
    CHECK(h.run.error().empty());

    // 3 pairings x 2 games x 2 cycles.
    CHECK(h.games.size() == 12);
    REQUIRE(h.run.observer());
    CHECK(h.run.observer()->names().size() == 3);
    std::set<std::pair<int, int>> colours;
    for (const auto& g : h.games) colours.insert({g.white, g.black});
    CHECK(colours.size() == 6);  // every ordered pair had White at least once

    const auto summary = tour::summarize(h.run.tournament()->dir());
    REQUIRE(summary);
    CHECK(summary->spec.name == "Three randoms");
    CHECK(summary->finished);

    // fastchess does not keep its timeouts in tournament.json: engines must still get the GUI's startup time
    // (Windows scans a new executable before it can answer), in the run and in the probes after it.
    REQUIRE(fastchess::config::TournamentConfig);
    CHECK(fastchess::config::TournamentConfig->startup_time == ENGINE_STARTUP_TIME);
    // Nor be stopped at once by a stop flag left behind by the run.
    CHECK_FALSE(fastchess::atomic::stop.load());
}

TEST_CASE("a finished round robin extended by a cycle resumes to play exactly the new games") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }

    const auto root = fs::temp_directory_path() / "fcg-run-tests-edit";
    fs::remove_all(root);
    auto engines = randomEngines();
    engines.push_back(engines[0]);
    engines[2].name = "random-c";

    MatchSetup setup   = randomSetup(0);
    setup.format       = TournamentFormat::RoundRobin;
    setup.participants = {{"random-a"}, {"random-b"}, {"random-c"}};
    const auto cores   = topologyFromCores({{0, 0}, {0, 1}, {0, 2}, {0, 3}});

    fs::path dir;
    {
        Harness h;
        REQUIRE(h.run.start(setup, engines, cores, root).empty());
        REQUIRE(h.waitFor([&] { return h.run.state() == RunController::State::Finished; }, 30s));
        CHECK(h.games.size() == 6);
        dir = h.run.tournament()->dir();

        tour::Edit more;
        more.kind   = tour::Edit::Kind::SetCycles;
        more.cycles = 2;
        CHECK(h.run.applyEdit(more).empty());
        CHECK(h.run.info().total_games == 12);
    }

    const auto unfinished = unfinishedMatch(root);
    REQUIRE(unfinished);
    CHECK(unfinished->played == 6);
    CHECK(unfinished->total == 12u);

    Harness h;
    REQUIRE(h.run.resume(dir).empty());
    REQUIRE(h.waitFor([&] { return h.run.state() == RunController::State::Finished; }, 30s));
    CHECK(h.run.error().empty());
    CHECK(h.games.size() == 12);
}

TEST_CASE("headless: a setup file becomes a tournament played to the end; the queue is stored") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }

    const auto profile = fs::temp_directory_path() / "fcg-run-tests-headless";
    fs::remove_all(profile);
    fs::create_directories(profile);
    const Storage storage(profile);
    storage.saveEngines(randomEngines());

    MatchSetup setup = randomSetup(10);
    REQUIRE(writeJson(profile / "setup-file.json", setup));

    const auto cores = topologyFromCores({{0, 0}, {0, 1}});
    CHECK(runHeadless((profile / "setup-file.json").string(), storage, cores) == 0);

    const auto dirs = tour::tournamentDirs(storage.runsDir());
    REQUIRE(dirs.size() == 1);
    const auto summary = tour::summarize(dirs[0]);
    REQUIRE(summary);
    CHECK(summary->finished);
    CHECK(summary->played == 10);

    // Running a finished tournament again does nothing and succeeds.
    CHECK(runHeadless(dirs[0].string(), storage, cores) == 0);

    std::vector<QueuedTournament> queue = {{"first", setup}, {"second", setup}};
    storage.saveQueue(queue);
    const auto loaded = storage.loadQueue();
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[1].label == "second");
    CHECK(loaded[1].setup.fixed_games == 10);
}

TEST_CASE("headless: the processor use of each game is kept in processor.tsv, as the window keeps it") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }

    // Each move takes a few milliseconds, so that the engines' thinking lasts long enough to be measured.
    setEnv("FCG_MOCK_MOVE_MS", "3");
    // Without a broadcast (no observer of the games but the one measuring), and with one (the window's observer).
    for (const bool broadcasting : {false, true}) {
        CAPTURE(broadcasting);
        const auto profile = fs::temp_directory_path() / "fcg-run-tests-headless-cpu";
        fs::remove_all(profile);
        fs::create_directories(profile);
        const Storage storage(profile);
        storage.saveEngines(randomEngines());
        REQUIRE(writeJson(profile / "setup-file.json", randomSetup(4)));
        HeadlessBroadcast broadcast;
        if (broadcasting) broadcast.live_dir = (profile / "live").string();
        const auto code = runHeadless((profile / "setup-file.json").string(), storage,
                                      topologyFromCores({{0, 0}, {0, 1}}), broadcast);
        REQUIRE(code == 0);

        const auto dirs = tour::tournamentDirs(storage.runsDir());
        REQUIRE(dirs.size() == 1);
        std::set<uint64_t> offsets;
        for (const auto& result : tour::TournamentState::open(dirs[0])->results()) offsets.insert(result.pgn.offset);
        REQUIRE(offsets.size() == 4);

        // One line per game, by its PGN offset, with the time White's and Black's engines thought.
        std::ifstream in(dirs[0] / "processor.tsv", std::ios::binary);
        std::set<uint64_t> lines;
        for (std::string text; std::getline(in, text);) {
            const auto line = parseGameCpuLine(text);
            REQUIRE(line);
            CHECK(line->cpu.own_wall_ms[0] > 0);
            CHECK(line->cpu.own_wall_ms[1] > 0);
            lines.insert(line->offset);
        }
        CHECK(lines == offsets);
        in.close();
        fs::remove_all(profile);
    }
    setEnv("FCG_MOCK_MOVE_MS", nullptr);
}

TEST_CASE("a Swiss and a knockout are played to the end through the GUI's controller") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }

    const auto root = fs::temp_directory_path() / "fcg-run-tests-swiss";
    fs::remove_all(root);
    auto engines = randomEngines();
    for (const char* name : {"random-c", "random-d", "random-e"}) {
        engines.push_back(engines[0]);
        engines.back().name = name;
    }
    const auto cores = topologyFromCores({{0, 0}, {0, 1}, {0, 2}, {0, 3}});

    MatchSetup setup   = randomSetup(0);
    setup.format       = TournamentFormat::Swiss;
    setup.cycles       = 3;  // rounds
    setup.participants = {{"random-a"}, {"random-b"}, {"random-c"}, {"random-d"}, {"random-e"}};
    {
        Harness h;
        REQUIRE(h.run.start(setup, engines, cores, root).empty());
        REQUIRE(h.waitFor([&] { return h.run.state() == RunController::State::Finished; }, 30s));
        CHECK(h.run.error().empty());
        CHECK(h.games.size() == 3u * 2u * 2u);  // 3 rounds, 2 pairings (one bye), 2 games
        CHECK(h.run.tournament()->byes().size() == 3);
    }

    setup.format = TournamentFormat::Knockout;
    setup.cycles = 1;
    Harness h;
    REQUIRE(h.run.start(setup, engines, cores, root).empty());
    REQUIRE(h.waitFor([&] { return h.run.state() == RunController::State::Finished; }, 60s));
    CHECK(h.run.error().empty());
    CHECK(h.run.tournament()->finished());
    const auto brackets = h.run.tournament()->brackets();
    REQUIRE_FALSE(brackets.empty());
    CHECK(brackets.rbegin()->second.size() == 1);  // a final
    CHECK(h.games.size() >= 4u * 2u);              // 4 matches, plus tiebreaks
}

TEST_CASE("the engines' search output reaches the live view while they think") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }

    const auto root = fs::temp_directory_path() / "fcg-run-tests-lines";
    fs::remove_all(root);

    Harness h;
    REQUIRE(h.run.start(randomSetup(20), randomEngines(), topologyFromCores({{0, 0}, {0, 1}}), root).empty());

    // The random mover prints "info depth 1 score cp 0 pv <move>" before every bestmove.
    std::vector<SlotState> slots(2);
    bool seen = false;
    h.waitFor(
        [&] {
            auto* obs = h.run.observer();
            for (int i = 0; obs && i < obs->slotCount() && i < 2; ++i) {
                obs->copySlot(i, slots[static_cast<std::size_t>(i)]);
                for (const auto& line : slots[static_cast<std::size_t>(i)].info) {
                    if (line.rfind("info depth 1 score cp 0 pv ", 0) == 0) seen = true;
                }
            }
            return seen || h.run.state() == RunController::State::Finished;
        },
        30s);
    CHECK(seen);

    h.run.stopWhenIdle();
    REQUIRE(h.waitFor([&] { return h.run.state() == RunController::State::Finished; }, 30s));
}

TEST_CASE("headless: the tournament is broadcast over TLCS and as live files") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }

    const auto profile = fs::temp_directory_path() / "fcg-run-tests-headless-broadcast";
    fs::remove_all(profile);
    fs::create_directories(profile);
    const Storage storage(profile);
    storage.saveEngines(randomEngines());
    REQUIRE(writeJson(profile / "setup-file.json", randomSetup(200)));

    HeadlessBroadcast broadcast;
    broadcast.tlcs_port = 26041;
    broadcast.live_dir  = (profile / "live").string();

    std::atomic_int code{-1};
    std::thread play([&] {
        code = runHeadless((profile / "setup-file.json").string(), storage, topologyFromCores({{0, 0}, {0, 1}}),
                           broadcast);
    });

    // A viewer: logs on until the server answers, then acknowledges and keeps what it receives.
    net::UdpSocket client;
    REQUIRE(client.bind(26042).empty());
    const net::Endpoint to{0x7F000001, 26041};
    std::vector<std::string> received;
    bool logged_on   = false;
    const auto until = std::chrono::steady_clock::now() + 60s;
    auto next_logon  = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < until && (code < 0 || !logged_on)) {
        if (!logged_on && std::chrono::steady_clock::now() >= next_logon) {
            client.send(to, "LOGONv15:tester");
            next_logon = std::chrono::steady_clock::now() + 100ms;
        }
        net::Endpoint from;
        const auto msg = client.receive(from, 50);
        if (!msg) continue;
        if (*msg == "LOGON SUCCESSFUL") logged_on = true;
        if (msg->front() == '<') {
            const auto close = msg->find('>');
            client.send(to, "ACK: " + msg->substr(1, close - 1));
            received.push_back(msg->substr(close + 1));
        } else {
            received.push_back(*msg);
        }
    }
    play.join();
    CHECK(code == 0);
    CHECK(logged_on);
    for (;;) {  // what is still on its way
        net::Endpoint from;
        const auto msg = client.receive(from, 200);
        if (!msg) break;
        const auto close = msg->front() == '<' ? msg->find('>') : std::string::npos;
        received.push_back(close == std::string::npos ? *msg : msg->substr(close + 1));
    }

    const auto count = [&](const std::string& prefix) {
        return std::count_if(received.begin(), received.end(),
                             [&](const std::string& m) { return m.rfind(prefix, 0) == 0; });
    };
    CHECK(count("WPLAYER: ") > 0);
    CHECK(count("WMOVE: ") > 0);
    // Random games last a few milliseconds, several end between two snapshots: the last one is told at least.
    CHECK(count("result: ") > 0);
    CHECK(count("CT: total games = ") > 0);

    // The live files describe the finished tournament.
    std::ifstream json(profile / "live" / "tournament.json");
    const std::string text((std::istreambuf_iterator<char>(json)), std::istreambuf_iterator<char>());
    CHECK(text.find("\"finished\":true") != std::string::npos);
    CHECK(fs::is_regular_file(profile / "live" / "standings.html"));
}

TEST_CASE("quick test: ten games against the engine itself, then a report on its health and its clock") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }

    const auto root = fs::temp_directory_path() / "fcg-run-tests-quick";
    fs::remove_all(root);

    auto library     = randomEngines();
    auto engines     = library;
    const auto setup = quickTestSetup(library[0], "", root / "quick-test.epd", engines);
    REQUIRE(engines.size() == 3);  // a copy of the engine, to play itself
    CHECK(setup.engine_b == "random-a (2)");
    CHECK(fs::is_regular_file(root / "quick-test.epd"));

    Harness h;
    REQUIRE(h.run.start(setup, engines, topologyFromCores({{0, 0}, {0, 1}, {0, 2}, {0, 3}}), root).empty());
    REQUIRE(h.waitFor([&] { return h.run.state() == RunController::State::Finished; }, 60s));
    CHECK(h.run.error().empty());
    CHECK(h.games.size() == 10);

    const auto* t     = h.run.tournament();
    const auto totals = h.run.observer()->totals();
    const auto report = quickTestReport(*t, t->spec().participants.at(0).id, totals.at(0).time_ms, totals.at(0).moves);
    CHECK(report.ok);
    CHECK(report.message.find("10 games") != std::string::npos);
    CHECK(report.message.find("0 lost on time, 0 crashes, 0 illegal moves") != std::string::npos);
}

TEST_CASE("a read or a check interrupted for a start ends at once instead of waiting for a slow engine") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }
    if (!fastchess::config::TournamentConfig)
        fastchess::config::TournamentConfig = std::make_unique<fastchess::config::Tournament>();
    // The engine takes 20 s to answer uci, like one loading a large network.
    setEnv("FCG_MOCK_SLOW_UCIOK", "20000");

    EngineEntry entry;
    entry.name = "slow";
    entry.cmd  = randomMover();
    // Interrupted once it waits for the engine: its result, and how long it took to end once its engine was ended.
    const auto interrupt = [](auto job, std::atomic_bool& interrupted) {
        auto running = std::async(std::launch::async, job);
        CHECK(running.wait_for(1s) == std::future_status::timeout);  // it waits for the engine
        const auto t0 = std::chrono::steady_clock::now();
        interrupted   = true;
        interruptEngineProcesses();
        // Ended again until it is over, as the GUI does at every frame: it may have been starting one.
        while (running.wait_for(50ms) != std::future_status::ready) interruptEngineProcesses();
        const auto took = std::chrono::steady_clock::now() - t0;
        return std::make_pair(running.get(), took);
    };

    // A read finding out the protocol: given up, rather than starting the engine again to read its options.
    std::atomic_bool interrupted{false};
    const auto [read, read_took] = interrupt([&] { return probeEngine(entry, &interrupted); }, interrupted);
    CHECK_FALSE(read.ok);
    CHECK(read.error == "interrupted");
    CHECK(read_took < 5s);

    // A read of an engine whose protocol is known, and a check: they fail as if the engine had stopped.
    entry.protocol                 = "uci";
    interrupted                    = false;
    const auto [known, known_took] = interrupt([&] { return probeEngine(entry, &interrupted); }, interrupted);
    CHECK_FALSE(known.ok);
    CHECK(known_took < 5s);
    interrupted                    = false;
    const auto [check, check_took] = interrupt([&] { return checkEngine(entry, &interrupted); }, interrupted);
    CHECK_FALSE(check.ok);
    CHECK(check_took < 5s);
    CHECK(engineProcesses() == 0);

#ifndef _WIN32
    // An engine behind a wrapper script that does not exec: the script's own child keeps the engine's output open,
    // and would go on running. Both are ended, at once.
    const auto dir = fs::temp_directory_path() / "fcg-interrupt-wrapper";
    fs::create_directories(dir);
    const auto script = dir / "slow.sh";
    const auto pidof  = dir / "pid";
    fs::remove(pidof);
    {
        std::ofstream out(script);
        out << "#!/bin/sh\necho $$ > '" << pidof.string() << "'\n'" << randomMover() << "' \"$@\"\n";
    }
    fs::permissions(script, fs::perms::owner_all);
    entry.cmd    = script.string();
    interrupted  = false;
    auto wrapped = std::async(std::launch::async, [&] { return probeEngine(entry, &interrupted); });
    CHECK(wrapped.wait_for(1s) == std::future_status::timeout);
    int shell = 0;
    std::ifstream(pidof) >> shell;
    REQUIRE(shell > 0);
    const auto engines = childProcesses(shell);
    REQUIRE(engines.size() == 1);  // the engine the script started
    const auto t0 = std::chrono::steady_clock::now();
    interrupted   = true;
    interruptEngineProcesses();
    while (wrapped.wait_for(50ms) != std::future_status::ready) interruptEngineProcesses();
    CHECK(std::chrono::steady_clock::now() - t0 < 5s);
    CHECK_FALSE(wrapped.get().ok);
    CHECK(engineProcesses() == 0);
    // Given to init when the script was killed, and reaped by it.
    bool gone = false;
    for (int i = 0; i < 40 && !gone; ++i) {
        gone = kill(engines[0], 0) == -1 && errno == ESRCH;
        if (!gone) std::this_thread::sleep_for(50ms);
    }
    CHECK(gone);
    fs::remove_all(dir);
#endif

    setEnv("FCG_MOCK_SLOW_UCIOK", nullptr);
}

#ifndef _WIN32
TEST_CASE("a process tree is ended only from a child of ours not reaped yet, never from a pid that may be reused") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }
    // fastchess reaps an engine that ended when it next looks at it (alive()), and takes it off the process list
    // only later: meanwhile its pid may be given to another process, which must not be signalled.
    fastchess::engine::process::Process process;
    REQUIRE(process.init("", randomMover(), "", "reaped"));
    const auto pid = static_cast<int>(process.pid());
    REQUIRE(pid > 0);
    CHECK(process.writeInput("quit\n"));
    bool ended = false;
    for (int i = 0; i < 100 && !ended; ++i) {
        ended = !process.alive();
        if (!ended) std::this_thread::sleep_for(50ms);
    }
    REQUIRE(ended);
    CHECK(engineProcesses() == 1);  // reaped, still listed
    CHECK_FALSE(killProcessTree(pid));
    CHECK_FALSE(killProcessTree(getppid()));  // never a child of ours
    process.terminate();
    CHECK(engineProcesses() == 0);

    // A child of ours that runs: ended, with nothing it started left.
    fastchess::engine::process::Process running;
    REQUIRE(running.init("", randomMover(), "", "running"));
    CHECK(killProcessTree(static_cast<int>(running.pid())));
    ended = false;
    for (int i = 0; i < 100 && !ended; ++i) {
        ended = !running.alive();
        if (!ended) std::this_thread::sleep_for(20ms);
    }
    CHECK(ended);
}
#endif

TEST_CASE("EPD suite runs: every engine on every position, the totals, CSV and saved runs") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }
    if (!fastchess::config::TournamentConfig)
        fastchess::config::TournamentConfig = std::make_unique<fastchess::config::Tournament>();

    // One legal move in both: found in the first, avoided in vain in the second.
    tour::Suite suite;
    suite.positions.resize(2);
    REQUIRE(tour::parseSuiteLine("k7/8/8/8/8/8/1q6/K7 w - - bm Kxb2; id \"forced\";", suite.positions[0]));
    REQUIRE(tour::parseSuiteLine("k7/8/8/8/8/8/1q6/K7 w - - am Kxb2;", suite.positions[1]));
    suite.positions[1].id = "#2";

    std::vector<fastchess::EngineConfiguration> engines(2);
    for (int i = 0; i < 2; ++i) {
        engines[i].name = "random" + std::to_string(i);
        engines[i].cmd  = randomMover();
    }
    SuiteSetup setup;
    setup.move_s = 0.05;
    SuiteRunner runner;
    runner.start(suite, engines, setup, 3);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (runner.running() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE_FALSE(runner.running());
    CHECK(runner.answered() == 4);

    const auto columns = runner.columns();
    REQUIRE(columns.size() == 2);
    for (const auto& c : columns) {
        REQUIRE(c.answers.size() == 2);
        REQUIRE(c.answers[0]);
        CHECK(c.answers[0]->solved);
        CHECK(c.answers[0]->move == "Kxb2");
        CHECK_FALSE(c.answers[1]->solved);
        const auto t = c.totals();
        CHECK(t.answered == 2);
        CHECK(t.solved == 1);
    }

    const auto csv = suiteCsv(suite, columns);
    CHECK(csv.rfind("id,solution,random0,random0 ms,random1,random1 ms\n", 0) == 0);
    CHECK(csv.find("forced,Kxb2,Kxb2 ✓,") != std::string::npos);
    CHECK(csv.find("total,,1/2,") != std::string::npos);

    // Kept, then found again for the same suite only, and read back.
    const auto dir = fs::temp_directory_path() / "fcg-suite-runs";
    fs::remove_all(dir);
    const auto saved = saveSuiteRun(dir, "/books/bk.epd", 2, columns);
    REQUIRE_FALSE(saved.empty());
    CHECK(savedSuiteRuns(dir, "/elsewhere/bk.epd", 2).size() == 1);
    CHECK(savedSuiteRuns(dir, "/books/bk.epd", 3).empty());
    CHECK(savedSuiteRuns(dir, "/books/wac.epd", 2).empty());
    const auto back = loadSuiteRun(saved);
    REQUIRE(back.size() == 2);
    CHECK(back[1].engine == "random1");
    CHECK_FALSE(back[1].when.empty());
    REQUIRE(back[1].answers[0]);
    CHECK(back[1].answers[0]->move == "Kxb2");
    fs::remove_all(dir);
}

#ifdef _WIN32
TEST_CASE("on Windows, what an engine starts is in the engine's job and ends with it") {
    // A .bat that starts the engine: cmd.exe is the process started, the engine its child. The engine is this program
    // (see main.cpp), which reads nothing, leaves its console and tells once it runs: the random mover, found as soon
    // as cmd.exe created it, could still be starting when cmd.exe was killed, attached to the console cmd.exe owned
    // and reading the input cmd.exe passed on, and once ended with it on GitHub's runners (#111).
    std::error_code ec;
    const auto dir = fs::temp_directory_path() / "fcg-job-tests";
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const auto pid_file = dir / "engine.pid";
    std::ofstream(dir / "engine.bat") << "@\"" << self().string() << "\" --job-child \"" << pid_file.string()
                                      << "\"\r\n";

    char system_dir[MAX_PATH];
    const auto n   = GetSystemDirectoryA(system_dir, MAX_PATH);
    const auto cmd = (fs::path(std::string(system_dir, n)) / "cmd.exe").string();

    HANDLE engine = nullptr;
    {
        fastchess::engine::process::Process process;
        process.setRealtimeLogging(false);
        REQUIRE(process.init(dir.string(), cmd, "/c \"" + (dir / "engine.bat").string() + "\"", "job"));
        const HANDLE started = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(process.pid()));
        REQUIRE(started);

        // The engine, once it runs: it still does then, so its id is not another process'.
        DWORD engine_pid = 0;
        for (int i = 0; i < 300 && !engine_pid; ++i) {
            std::ifstream(pid_file) >> engine_pid;
            if (!engine_pid) std::this_thread::sleep_for(50ms);
        }
        REQUIRE(engine_pid != 0);
        engine = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, engine_pid);
        REQUIRE(engine);

        // Started by cmd.exe, and in the job.
        DWORD parent          = 0;
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        for (BOOL more = Process32First(snapshot, &entry); more; more = Process32Next(snapshot, &entry)) {
            if (entry.th32ProcessID == engine_pid) parent = entry.th32ParentProcessID;
        }
        CloseHandle(snapshot);
        CHECK(static_cast<int64_t>(parent) == process.pid());
        BOOL in_job = FALSE;
        CHECK_MESSAGE(IsProcessInJob(engine, nullptr, &in_job), GetLastError());
        CHECK(in_job);

        // cmd.exe ended abruptly: the engine it started still runs, until the process is ended.
        process.forceKill();
        CHECK(WaitForSingleObject(started, 5000) == WAIT_OBJECT_0);
        CloseHandle(started);
        DWORD code = 0;
        CHECK_MESSAGE(WaitForSingleObject(engine, 500) == WAIT_TIMEOUT,
                      "engine exit code: " << (GetExitCodeProcess(engine, &code), code));
    }
    CHECK(WaitForSingleObject(engine, 5000) == WAIT_OBJECT_0);
    CloseHandle(engine);
    fs::remove_all(dir, ec);
}
#endif
