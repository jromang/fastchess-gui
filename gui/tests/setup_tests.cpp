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

TEST_CASE("the PGN fields the charts read: on by default, and on for a setup saved with the old default") {
    const auto fields = [](const MatchSetup& s) {
        return std::vector<bool>{s.pgn_nodes, s.pgn_seldepth, s.pgn_nps, s.pgn_pv, s.pgn_tbhits, s.pgn_decision};
    };
    const auto saved = [](const nlohmann::json& j) {
        auto s = j.get<MatchSetup>();
        upgradeSavedSetup(j, s);
        return s;
    };
    const std::vector<bool> on(6, true), off(6, false);
    CHECK(fields(MatchSetup{}) == on);
    CHECK(fields(nlohmann::json::object().get<MatchSetup>()) == on);

    // Saved before the revision, with every field off as they were by default: read with them on.
    MatchSetup old;
    old.pgn_nodes = old.pgn_seldepth = old.pgn_nps = old.pgn_pv = old.pgn_tbhits = old.pgn_decision = false;
    auto j = nlohmann::json(old);
    CHECK(j.at("revision") == 1);
    j.erase("revision");
    CHECK(fields(saved(j)) == on);
    CHECK(saved(j).revision == 1);
    // As it is, not upgraded: a setup file given to --run.
    CHECK(fields(j.get<MatchSetup>()) == off);

    // Saved by 0.4.0 or earlier, before tbhits and the decision time were PGN fields: the same.
    auto older = j;
    older.erase("pgn_tbhits");
    older.erase("pgn_decision");
    CHECK(fields(saved(older)) == on);

    // One of them turned on then was a choice: kept as it was, the fields it did not have off.
    j["pgn_pv"]   = true;
    const auto pv = saved(j);
    CHECK(pv.pgn_pv);
    CHECK_FALSE(pv.pgn_nodes);
    CHECK_FALSE(pv.pgn_decision);
    older["pgn_nodes"] = true;
    const auto nodes   = saved(older);
    CHECK(nodes.pgn_nodes);
    CHECK_FALSE(nodes.pgn_pv);
    CHECK(nodes.pgn_tbhits);  // not in the file: the default, as for any field added since
    CHECK(nodes.pgn_decision);

    // Turned off since the revision: kept off.
    CHECK(fields(saved(nlohmann::json(old))) == off);
}

TEST_CASE("the PGN fields: the profile's setup, queue and templates are upgraded, a setup file for --run is not") {
    const auto fields = [](const MatchSetup& s) {
        return std::vector<bool>{s.pgn_nodes, s.pgn_seldepth, s.pgn_nps, s.pgn_pv, s.pgn_tbhits, s.pgn_decision};
    };
    const std::vector<bool> on(6, true), off(6, false);
    const auto dir = fs::temp_directory_path() / "fcg-tests-pgn-fields";
    fs::remove_all(dir);
    fs::create_directories(dir / "templates");

    // As 0.4.0 wrote them: no revision, nor tbhits and the decision time.
    MatchSetup old;
    old.pgn_nodes = old.pgn_seldepth = old.pgn_nps = old.pgn_pv = false;
    auto j                                                      = nlohmann::json(old);
    j.erase("revision");
    j.erase("pgn_tbhits");
    j.erase("pgn_decision");
    auto queue = nlohmann::json::array({{{"label", "next"}, {"setup", j}}, {{"label", "empty"}}});
    REQUIRE(writeJson(dir / "setup.json", j));
    REQUIRE(writeJson(dir / "queue.json", queue));
    REQUIRE(writeJson(dir / "templates" / "old.json", j));
    // A hand-written setup file with every field off on purpose.
    auto mine = nlohmann::json(old);
    mine.erase("revision");
    mine["pgn_tbhits"] = mine["pgn_decision"] = false;
    REQUIRE(writeJson(dir / "mine.json", mine));

    const Storage storage(dir);
    CHECK(fields(storage.loadSetup()) == on);
    const auto loaded = storage.loadQueue();
    REQUIRE(loaded.size() == 2);
    CHECK(fields(loaded[0].setup) == on);
    CHECK(fields(loaded[1].setup) == on);
    REQUIRE(readSavedSetup(dir / "templates" / "old.json"));
    CHECK(fields(*readSavedSetup(dir / "templates" / "old.json")) == on);
    // --run reads its file with readJson(): as written.
    REQUIRE(readJson<MatchSetup>(dir / "mine.json"));
    CHECK(fields(*readJson<MatchSetup>(dir / "mine.json")) == off);

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

    // Another window of the profile plays it: its marker is left alone, whatever this window's settings.
    CHECK(launchResume(true, false, running, true, true, true) == LaunchResume::Busy);
    CHECK(launchResume(false, true, paused, false, false, true) == LaunchResume::Busy);
    CHECK(launchResume(true, false, std::nullopt, true, true, true) == LaunchResume::Nothing);

    // The program ended while it resumed it at the last launch: not resumed again, unless it is over.
    const RunningMarker resuming{"/runs/t", false, true};
    CHECK(launchResume(true, false, resuming, true, true) == LaunchResume::Crashed);
    CHECK(launchResume(true, false, resuming, false, false) == LaunchResume::Queue);
    CHECK(launchResume(false, false, resuming, true, true) == LaunchResume::Nothing);

    // When the run ends the marker stays only if the window quits while it runs; a stop the user asked for removes
    // it, even when they quit before the engines stopped.
    CHECK(keepMarkerAtEnd(true, false));
    CHECK_FALSE(keepMarkerAtEnd(true, true));
    CHECK_FALSE(keepMarkerAtEnd(false, false));
    CHECK_FALSE(keepMarkerAtEnd(false, true));

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
    CHECK_FALSE(storage.runningMarker()->resuming);
    storage.setRunningMarker(std::nullopt);
    CHECK_FALSE(storage.runningMarker());
    CHECK_FALSE(fs::exists(dir / "running.json"));

    // The resume flag is kept; a window removes only the marker of its own tournament.
    storage.setRunningMarker(resuming);
    REQUIRE(storage.runningMarker());
    CHECK(storage.runningMarker()->resuming);
    storage.setRunningMarker(RunningMarker{"/runs/u"});
    storage.clearRunningMarker("/runs/t");
    REQUIRE(storage.runningMarker());
    CHECK(storage.runningMarker()->dir == "/runs/u");
    CHECK(markerNames(storage.runningMarker(), "/runs/u"));
    CHECK_FALSE(markerNames(std::nullopt, "/runs/u"));
    storage.clearRunningMarker("/runs/u");
    CHECK_FALSE(storage.runningMarker());
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

TEST_CASE("a start waits only for the read of an engine it plays; other reads and checks are interrupted") {
    using A                              = StartGate::Action;
    const std::vector<std::string> match = {"Alpha", "Beta"};
    const std::vector<std::string> none;

    // Nothing in flight, nothing of its own queued: at once, whatever else waits to be read.
    CHECK(startGate(match, "", "", {"Gamma", "Delta"}, false).action == A::Start);
    // A resume plays the engines copied in its folder: a read of the library is interrupted, not waited for.
    auto gate = startGate(none, "Gamma", "", {"Alpha"}, false);
    CHECK(gate.action == A::Interrupt);
    CHECK(gate.interrupt_read);
    CHECK_FALSE(gate.interrupt_check);
    CHECK(startGate(none, "", "", {"Alpha", "Beta"}, false).action == A::Start);

    // The read of an engine it plays: waited for, and a check in flight meanwhile goes on (it is ended afterwards).
    gate = startGate(match, "Beta", "Gamma", {}, false);
    CHECK(gate.action == A::Wait);
    CHECK(gate.engine == "Beta");
    CHECK_FALSE(gate.check);
    CHECK_FALSE(gate.suite);
    // The read of another engine, a check of any: interrupted together.
    gate = startGate(match, "Gamma", "Alpha", {"Beta"}, false);
    CHECK(gate.action == A::Interrupt);
    CHECK(gate.interrupt_read);
    CHECK(gate.interrupt_check);
    // Then its own engine still waiting to be read: read first, the first of the queue that it plays.
    gate = startGate(match, "", "", {"Gamma", "Beta", "Alpha"}, false);
    CHECK(gate.action == A::Read);
    CHECK(gate.engine == "Beta");

    // The engines a setup plays.
    MatchSetup setup;
    setup.engine_a = "Alpha";
    setup.engine_b = "Beta";
    CHECK(enginesOfSetup(setup) == match);
    setup.format       = TournamentFormat::RoundRobin;
    setup.participants = {{.name = "Gamma"}, {.name = "Delta"}, {.name = "Alpha"}};
    CHECK(enginesOfSetup(setup) == std::vector<std::string>{"Gamma", "Delta", "Alpha"});
}

TEST_CASE("a start waits for a test suite to end, and a test suite does not start while a tournament runs or waits") {
    using A                              = StartGate::Action;
    const std::vector<std::string> match = {"Alpha", "Beta"};

    // Whatever is read or checked, and even with nothing in flight: it waits for the suite, and ends nothing (an
    // interruption would end the suite's engines too).
    const std::vector<std::pair<std::string, std::string>> in_flight = {
        {"", ""}, {"Gamma", ""}, {"", "Delta"}, {"Alpha", "Delta"}};
    for (const auto& [reading, checking] : in_flight) {
        const auto gate = startGate(match, reading, checking, {"Beta"}, true);
        CHECK(gate.action == A::Wait);
        CHECK(gate.suite);
        CHECK_FALSE(gate.interrupt_read);
        CHECK_FALSE(gate.interrupt_check);
    }
    // A resume too: it replaces fastchess' configuration as any start.
    CHECK(startGate({}, "", "", {}, true).action == A::Wait);

    CHECK(suiteMayStart(false, false));
    CHECK_FALSE(suiteMayStart(true, false));  // a tournament or a quick test runs: its end ends every engine
    CHECK_FALSE(suiteMayStart(false, true));  // a start waits: it would wait for the suite
}

TEST_CASE("a start waits for the PGN books it plays to be prepared, without interrupting reads for nothing") {
    using A                              = StartGate::Action;
    const std::vector<std::string> match = {"Alpha", "Beta"};
    const std::vector<std::string> books = {"Alpha (Openings)"};

    // Nothing else in flight: it waits for the book of that participant.
    auto gate = startGate(match, "", "", {}, false, books);
    CHECK(gate.action == A::Wait);
    CHECK(gate.book);
    CHECK(gate.engine == "Alpha (Openings)");
    // Its own engine being read: that read first, then the book.
    gate = startGate(match, "Beta", "", {}, false, books);
    CHECK(gate.action == A::Wait);
    CHECK_FALSE(gate.book);
    CHECK(gate.engine == "Beta");
    // Its own engine waiting to be read, nothing in flight: read meanwhile.
    gate = startGate(match, "", "", {"Beta"}, false, books);
    CHECK(gate.action == A::Read);
    CHECK(gate.engine == "Beta");
    // Another engine read, or checked: not interrupted while the book is prepared, which takes long enough for them
    // to end.
    gate = startGate(match, "Gamma", "Delta", {}, false, books);
    CHECK(gate.action == A::Wait);
    CHECK(gate.book);
    CHECK_FALSE(gate.interrupt_read);
    // The book ready: as without one.
    CHECK(startGate(match, "Gamma", "", {}, false, {}).action == A::Interrupt);
    CHECK(startGate(match, "", "", {}, false, {}).action == A::Start);
    // A test suite comes first, as always.
    CHECK(startGate(match, "", "", {}, true, books).suite);

    // The PGN books a setup needs prepared: its participants', under the names they play under; Polyglot books are
    // read as they are.
    MatchSetup setup;
    setup.engine_a     = "Alpha";
    setup.engine_b     = "Alpha";
    setup.book_a.path  = "/books/openings.pgn";
    setup.book_a.name  = "Openings";
    setup.book_a.depth = 12;
    setup.book_b.path  = "/books/other.bin";
    setup.chess960     = true;
    const auto needs   = pgnBookNeeds(setup);
    REQUIRE(needs.size() == 1);
    CHECK(needs[0].participant == "Alpha (Openings)");
    CHECK(needs[0].path == "/books/openings.pgn");
    CHECK(needs[0].depth == 12);
    CHECK(needs[0].chess960);
}

TEST_CASE("a start asked while another waits is refused, not put in its place") {
    CHECK(startAsked("", "start") == StartAsked::Go);
    // The same, pressed again or from another place: it goes on waiting.
    CHECK(startAsked("queue", "queue") == StartAsked::Waiting);
    CHECK(startAsked("test:Alpha", "test:Alpha") == StartAsked::Waiting);
    // Another one: the queue going on by itself is not dropped for a quick test pressed meanwhile, nor the other way.
    CHECK(startAsked("queue", "test:Alpha") == StartAsked::Refused);
    CHECK(startAsked("test:Alpha", "start") == StartAsked::Refused);
    CHECK(startAsked("test:Alpha", "test:Beta") == StartAsked::Refused);
}

TEST_CASE("a quick test pressed during the first read of an engine follows the engine to its id name") {
    StartRequest test{"test:alpha_dev", {"alpha_dev", "Beta"}};
    // The first read renames it: what it needs and its key follow, so that it starts once the read is over.
    CHECK(test.rename("alpha_dev", "Alpha 2.1"));
    CHECK(test.key == "test:Alpha 2.1");
    CHECK(test.needs == std::vector<std::string>{"Alpha 2.1", "Beta"});
    // Its opponent renamed: needed under its new name, the test is the same one.
    CHECK_FALSE(test.rename("Beta", "Beta 3"));
    CHECK(test.needs == std::vector<std::string>{"Alpha 2.1", "Beta 3"});
    CHECK(test.key == "test:Alpha 2.1");

    // Another start needing that engine: its needs follow, its key does not change.
    StartRequest start{"start", {"alpha_dev", "Beta"}};
    CHECK_FALSE(start.rename("alpha_dev", "Alpha 2.1"));
    CHECK(start.key == "start");
    CHECK(start.needs == std::vector<std::string>{"Alpha 2.1", "Beta"});
    // An engine whose name begins like another's is not taken for it.
    StartRequest other{"test:Alpha 2", {"Alpha 2"}};
    CHECK_FALSE(other.rename("Alpha", "Omega"));
    CHECK(other.key == "test:Alpha 2");
}

TEST_CASE("the queue starts its tournaments one by one: only the engines of the next one are waited for") {
    std::vector<QueuedTournament> queue(3);
    queue[0].setup.engine_a     = "A";
    queue[0].setup.engine_b     = "B";
    queue[1].setup.engine_a     = "C";
    queue[1].setup.engine_b     = "D";  // cannot be read
    queue[2].setup.format       = TournamentFormat::Gauntlet;
    queue[2].setup.participants = {{.name = "A"}, {.name = "E"}};

    // The first: its engines only, not D, whose read may fail, of the one after it.
    CHECK(queueNeeds(queue, 0) == std::vector<std::string>{"A", "B"});
    CHECK(queueNeeds(queue, 1) == std::vector<std::string>{"C", "D"});
    CHECK(queueNeeds(queue, 2) == std::vector<std::string>{"A", "E"});
    CHECK(queueNeeds(queue, 3).empty());

    // The second could not start (D cannot be read): kept, and the queue going on by itself tries the third.
    CHECK(queueNextAfterFailure(1, queue.size(), true) == std::optional<std::size_t>(2));
    // Nothing after the last; "Start now" tries the first only.
    CHECK_FALSE(queueNextAfterFailure(2, queue.size(), true));
    CHECK_FALSE(queueNextAfterFailure(0, queue.size(), false));
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
