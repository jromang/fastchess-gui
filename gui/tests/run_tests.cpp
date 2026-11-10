// Plays real tournaments through RunController with fastchess' random mover engine.

#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

#include <core/config/config.hpp>
#include <core/globals/globals.hpp>

#include "engines.hpp"
#include "headless.hpp"
#include "net.hpp"
#include "run.hpp"
#include "storage.hpp"

#ifdef _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif

using namespace fcg;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

// The mock engine is built next to the test binary.
std::string randomMover() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    const auto n = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    const fs::path self(std::string(buffer, n));
#else
    const fs::path self = fs::read_symlink("/proc/self/exe");
#endif
    return (self.parent_path() / FCG_RANDOM_MOVER).string();
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

    // The random mover prints "info depth 1 pv <move> score cp 0" before every bestmove.
    std::vector<SlotState> slots(2);
    bool seen = false;
    h.waitFor(
        [&] {
            auto* obs = h.run.observer();
            for (int i = 0; obs && i < obs->slotCount() && i < 2; ++i) {
                obs->copySlot(i, slots[static_cast<std::size_t>(i)]);
                for (const auto& line : slots[static_cast<std::size_t>(i)].info) {
                    if (line.rfind("info depth 1 pv ", 0) == 0) seen = true;
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
