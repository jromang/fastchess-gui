// The checks and settings around a tournament's start: memory, engine options forced by the setup.

#include <doctest/doctest.hpp>

#include <vector>

#include "memory.hpp"

using namespace fcg;

TEST_CASE("memory: the hash tables of the engine processes fastchess keeps, the largest first") {
    // A match: one process of each engine per game, however many fastchess may keep.
    auto need = hashMemory({256, 256}, 4, false);
    CHECK(need.processes == 8);
    CHECK(need.hash_mb == 2048);

    // A gauntlet: the tested engine in every game; 2 × 8 playing and 31 waiting processes.
    std::vector<uint64_t> gauntlet(31, 64);
    gauntlet[0] = 1024;
    need        = hashMemory(gauntlet, 8, false);
    CHECK(need.processes == 47);
    CHECK(need.hash_mb == 8 * 1024 + 39 * 64);

    // A big round robin, one game at a time: two playing, at most 32 waiting.
    need = hashMemory(std::vector<uint64_t>(40, 16), 1, false);
    CHECK(need.processes == 34);
    CHECK(need.hash_mb == 34 * 16);

    // With affinity, a cache per game: two playing and 16 waiting in each.
    need = hashMemory(std::vector<uint64_t>(40, 16), 2, true);
    CHECK(need.processes == 36);

    CHECK(hashMemory({}, 4, false).hash_mb == 0);
}

TEST_CASE("memory: each participant's Hash, its own before the common one before the engine's") {
    std::vector<EngineEntry> engines(4);
    engines[0].name        = "set";  // its own setting
    engines[0].options     = {{"Hash", "128"}};
    engines[0].uci_options = {{"Hash", "spin", "16", "1", "4096"}};
    engines[1].name        = "default";  // the engine's default
    engines[1].uci_options = {{"Hash", "spin", "32", "1", "4096"}};
    engines[2].name        = "none";  // no Hash option: nothing forced
    engines[2].uci_options = {{"Threads", "spin", "1", "1", "64"}};
    engines[3].name        = "unread";  // options not known yet: forced, as buildRun() does

    MatchSetup setup;
    setup.format       = TournamentFormat::RoundRobin;
    setup.participants = {{"set"}, {"default"}, {"none"}, {"unread"}, {"missing"}};
    CHECK(setupHashes(setup, engines) == std::vector<uint64_t>{128, 32, 0, 0});

    setup.common_hash             = 256;
    setup.participants[1].hash    = 512;
    CHECK(setupHashes(setup, engines) == std::vector<uint64_t>{256, 512, 0, 256});

    setup.format   = TournamentFormat::Match;
    setup.engine_a = "default";
    setup.engine_b = "none";
    CHECK(setupHashes(setup, engines) == std::vector<uint64_t>{256, 0});

    // A tournament created already: what its participants were given, else the engine's default.
    tour::TournamentSpec spec;
    spec.participants.resize(3);
    spec.participants[0].engine.name    = "set";
    spec.participants[0].engine.options = {{"Hash", "1024"}};
    spec.participants[1].engine.name    = "default";
    spec.participants[2].engine.name    = "withdrawn";
    spec.participants[2].active         = false;
    CHECK(specHashes(spec, engines) == std::vector<uint64_t>{1024, 32});
}

TEST_CASE("memory: above the free memory is a warning, above the installed memory an error") {
    MemoryNeed need;
    need.hash_mb   = 3000;
    need.processes = 4;
    CHECK_FALSE(need.aboveAvailable());  // the machine's memory not known: nothing to say
    need.system = SystemMemory{8192, 4096};
    CHECK_FALSE(need.aboveAvailable());
    CHECK_FALSE(need.aboveInstalled());
    need.hash_mb = 5000;
    CHECK(need.aboveAvailable());
    CHECK_FALSE(need.aboveInstalled());
    CHECK(need.text().find("4.0 GB") != std::string::npos);
    need.hash_mb = 9000;
    CHECK(need.aboveInstalled());
    CHECK(need.text().find("cannot start") != std::string::npos);

    // The system answers on Linux and Windows.
    const auto memory = systemMemory();
    REQUIRE(memory);
    CHECK(memory->total_mb > 0);
    CHECK(memory->available_mb <= memory->total_mb);
}
