// Plays real tournaments through RunController with fastchess' random mover engine.

#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <core/config/config.hpp>
#include <core/globals/globals.hpp>

#include "engines.hpp"
#include "run.hpp"

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
    CHECK(obs->fastchessPgn().read(last.fastchess_span).has_value());
    CHECK(obs->chessbasePgn().read(last.chessbase_span).has_value());

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
