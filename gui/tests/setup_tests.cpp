// The checks and settings around a tournament's start: memory, engine options forced by the setup.

#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>
#include <vector>

#include "memory.hpp"
#include "run.hpp"
#include "storage.hpp"

using namespace fcg;
namespace fs = std::filesystem;

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

    setup.common_hash          = 256;
    setup.participants[1].hash = 512;
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

TEST_CASE("the setup's Syzygy path and OwnBook off are given to the engines that have the options") {
    const auto dir = fs::temp_directory_path() / "fcg-tests-forced-options";
    fs::create_directories(dir);
    // fastchess appends .exe to extension-less commands on Windows.
    const auto binary = dir / "engine.exe";
    std::ofstream(binary) << "x";

    std::vector<EngineEntry> engines(3);
    for (auto& e : engines) e.cmd = binary.string();
    engines[0].name        = "both";
    engines[0].uci_options = {{"SyzygyPath", "string", "<empty>"}, {"OwnBook", "check", "true"}};
    engines[1].name        = "neither";
    engines[1].uci_options = {{"Hash", "spin", "16", "1", "1024"}};
    engines[2].name        = "unread";  // not known to have them: left alone
    engines[2].options     = {{"OwnBook", "true"}};

    MatchSetup setup;
    setup.format       = TournamentFormat::RoundRobin;
    setup.participants = {{"both"}, {"neither"}, {"unread"}};
    setup.limit        = LimitKind::Nodes;
    setup.tb_paths     = "/tb/wdl";

    const auto topology = topologyFromCores({{0, 0}, {0, 1}});
    const auto option   = [](const fastchess::EngineConfiguration& c, const std::string& name) -> std::string {
        for (const auto& [k, v] : c.options) {
            if (k == name) return v;
        }
        return "-";
    };

    // An engine added to the tournament later, with options of its own.
    EngineEntry later = engines[0];
    later.name        = "later";
    later.options     = {{"SyzygyPath", "/own/tb"}, {"OwnBook", "false"}};

    // Off by default: nothing changes.
    auto run = buildRun(setup, engines, topology, dir);
    CHECK(option(run.engines[0], "SyzygyPath") == "-");
    CHECK(option(run.engines[0], "OwnBook") == "-");
    CHECK(run.spec.forced_options.empty());
    // An engine added later keeps its own options, not another participant's ("unread"'s OwnBook).
    auto config = participantConfig(later, run.spec);
    CHECK(option(config, "SyzygyPath") == "/own/tb");
    CHECK(option(config, "OwnBook") == "false");

    setup.engine_syzygy      = true;
    setup.engine_ownbook_off = true;
    run                      = buildRun(setup, engines, topology, dir);
    CHECK(option(run.engines[0], "SyzygyPath") == "/tb/wdl");
    CHECK(option(run.engines[0], "OwnBook") == "false");
    CHECK(option(run.engines[1], "SyzygyPath") == "-");
    CHECK(option(run.engines[1], "OwnBook") == "-");
    CHECK(option(run.engines[2], "OwnBook") == "true");

    // An engine added to the tournament later gets them too, also once the tournament is read back from its folder.
    config = participantConfig(later, run.spec);
    CHECK(option(config, "SyzygyPath") == "/tb/wdl");
    CHECK(option(config, "OwnBook") == "false");
    const auto stored = nlohmann::json(run.spec).get<tour::TournamentSpec>();
    config            = participantConfig(later, stored);
    CHECK(option(config, "SyzygyPath") == "/tb/wdl");
    CHECK(option(config, "OwnBook") == "false");
    // An engine that does not have them is left alone.
    EngineEntry plain = engines[1];
    plain.options     = {{"OwnBook", "true"}};
    config            = participantConfig(plain, stored);
    CHECK(option(config, "SyzygyPath") == "-");
    CHECK(option(config, "OwnBook") == "true");

    // Only what the setup forced: with the own books left alone, the engine added later keeps its own OwnBook.
    setup.engine_ownbook_off = false;
    run                      = buildRun(setup, engines, topology, dir);
    later.options            = {{"OwnBook", "true"}};
    config                   = participantConfig(later, run.spec);
    CHECK(option(config, "SyzygyPath") == "/tb/wdl");
    CHECK(option(config, "OwnBook") == "true");
    setup.engine_ownbook_off = true;

    // Kept with the setup, as templates and the queue store it.
    const auto back = nlohmann::json(setup).get<MatchSetup>();
    CHECK(back.engine_syzygy);
    CHECK(back.engine_ownbook_off);
    CHECK_FALSE(nlohmann::json::object().get<MatchSetup>().engine_syzygy);

    // Asked for without a directory: said before the start.
    setup.tb_paths.clear();
    CHECK_THROWS_WITH((void)buildRun(setup, engines, topology, dir), doctest::Contains("Syzygy"));

    fs::remove_all(dir);
}

TEST_CASE("at launch, the tournament running when the program ended resumes, then the queue") {
    const RunningMarker running{"/runs/t", false};
    const RunningMarker paused{"/runs/t", true};

    // Interrupted while it ran: resumed, unless the setting is off or --autostart starts the setup.
    CHECK(launchResume(true, false, running, true, true) == LaunchResume::Resume);
    CHECK(launchResume(true, false, running, true, false) == LaunchResume::Resume);
    CHECK(launchResume(false, false, running, true, false) == LaunchResume::Nothing);
    CHECK(launchResume(true, true, running, true, false) == LaunchResume::Nothing);
    // Paused by the user, or nothing was running (it ended, or the user stopped it): it waits.
    CHECK(launchResume(true, false, paused, true, false) == LaunchResume::Nothing);
    CHECK(launchResume(true, false, std::nullopt, true, false) == LaunchResume::Nothing);
    // Over or gone: the queue goes on, if there is one.
    CHECK(launchResume(true, false, running, false, false) == LaunchResume::Queue);
    CHECK(launchResume(true, false, running, false, true) == LaunchResume::Nothing);

    // On by default, and kept in the profile.
    CHECK(MatchSetup{}.resume_at_launch);
    CHECK(nlohmann::json::object().get<MatchSetup>().resume_at_launch);
    const auto dir = fs::temp_directory_path() / "fcg-tests-running-marker";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const Storage storage(dir);
    CHECK_FALSE(storage.runningMarker());
    storage.setRunningMarker(paused);
    REQUIRE(storage.runningMarker());
    CHECK(storage.runningMarker()->dir == "/runs/t");
    CHECK(storage.runningMarker()->paused);
    storage.setRunningMarker(std::nullopt);
    CHECK_FALSE(storage.runningMarker());
    CHECK_FALSE(fs::exists(dir / "running.json"));
    fs::remove_all(dir);
}

TEST_CASE("the games a setup plans, and about how long they last with a clock") {
    const auto topology = topologyFromCores({{0, 0}, {0, 1}, {0, 2}, {0, 3}});

    MatchSetup setup;
    setup.mode        = MatchMode::Fixed;
    setup.fixed_games = 101;
    CHECK(plannedGames(setup) == 102);
    setup.mode = MatchMode::Sprt;
    CHECK(plannedGames(setup) == 0);
    setup.sprt_max_games = 1000;
    CHECK(plannedGames(setup) == 1000);
    setup.mode = MatchMode::Infinite;
    CHECK(plannedGames(setup) == 0);

    setup.participants = {{"a"}, {"b"}, {"c"}, {"d"}, {"e"}};
    setup.cycles       = 2;
    setup.format       = TournamentFormat::RoundRobin;
    CHECK(plannedGames(setup) == 10 * 2 * 2);
    setup.format = TournamentFormat::Gauntlet;
    setup.seeds  = 2;
    CHECK(plannedGames(setup) == 2 * 3 * 2 * 2);
    setup.format = TournamentFormat::Swiss;  // cycles = rounds
    CHECK(plannedGames(setup) == 2 * 2 * 2);
    setup.format = TournamentFormat::Knockout;
    CHECK(plannedGames(setup) == 4 * 2 * 2);

    // 6 games of about 2 × (60 + 60 × 1) s, three at a time (physical cores - 1).
    setup.format = TournamentFormat::RoundRobin;
    setup.cycles = 1;
    setup.participants.resize(3);
    setup.limit     = LimitKind::Time;
    setup.tc_base_s = 60;
    setup.tc_inc_s  = 1;
    REQUIRE(plannedSeconds(setup, topology));
    CHECK(*plannedSeconds(setup, topology) == doctest::Approx(6 * 240.0 / 3));
    setup.concurrency_auto = false;
    setup.concurrency      = 2;
    CHECK(*plannedSeconds(setup, topology) == doctest::Approx(6 * 240.0 / 2));
    setup.limit  = LimitKind::MoveTime;
    setup.move_s = 0.5;
    CHECK(*plannedSeconds(setup, topology) == doctest::Approx(6 * 60.0 / 2));
    // No clock: no estimate.
    setup.limit = LimitKind::Nodes;
    CHECK_FALSE(plannedSeconds(setup, topology));
}

TEST_CASE("a template or a queued setup keeps the program's settings: its look, the live boards, the notifications") {
    MatchSetup current;
    current.language         = "fr";
    current.theme            = "dark";
    current.ui_zoom          = 125;
    current.tile_info        = true;
    current.live_seed_side   = false;
    current.live_arrows      = false;
    current.panes["engines"] = 0.3f;
    current.notify           = false;
    current.resume_at_launch = false;

    // A setup stored with the defaults, as a template or the queue holds it.
    MatchSetup next;
    next.name   = "stored";
    next.cycles = 4;
    keepProgramSettings(next, current);
    CHECK(next.language == "fr");
    CHECK(next.theme == "dark");
    CHECK(next.ui_zoom == 125);
    CHECK(next.tile_info);
    CHECK_FALSE(next.live_seed_side);
    CHECK_FALSE(next.live_arrows);
    CHECK(next.panes == current.panes);
    CHECK_FALSE(next.notify);
    CHECK_FALSE(next.resume_at_launch);
    // The tournament itself is the stored one.
    CHECK(next.name == "stored");
    CHECK(next.cycles == 4);
}
