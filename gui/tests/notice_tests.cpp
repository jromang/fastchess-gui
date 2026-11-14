// The actionable messages of the setup: each names the engine (and its option or folder), the resource and the fix.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "console.hpp"
#include "setup_checks.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

EngineEntry engine(const std::string& name, const std::string& threads_default = "1") {
    EngineEntry e;
    e.name        = name;
    e.cmd         = "/engines/" + name;
    e.uci_options = {{"Threads", "spin", threads_default, "1", "64", {}},
                     {"Hash", "spin", "16", "1", "1024", {}},
                     {"Style", "combo", "Normal", "", "", {"Normal", "Risky"}},
                     {"SyzygyPath", "string", "<empty>", "", "", {}}};
    return e;
}

MatchSetup roundRobin(std::vector<std::string> names) {
    MatchSetup s;
    s.format = TournamentFormat::RoundRobin;
    for (auto& n : names) s.participants.push_back({std::move(n)});
    s.limit            = LimitKind::Time;
    s.concurrency_auto = false;
    s.concurrency      = 7;
    return s;
}

bool hasFix(const Notice& n, Fix::Kind kind) {
    return std::any_of(n.fixes.begin(), n.fixes.end(), [&](const Fix& f) { return f.kind == kind; });
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

}  // namespace

TEST_CASE("threads: an engine whose own Threads are high is flagged, with Lower Threads in the library") {
    const CpuTopology machine{16, 8};
    std::vector<EngineEntry> engines{engine("Alpha", "16"), engine("Beta")};
    auto setup = roundRobin({"Alpha", "Beta"});

    // 7 games at a time with a clock on 8 cores: 1 thread for each game.
    CHECK(threadsPerGame(setup, machine) == 1);
    CHECK(engines[0].threads() == 16);  // its default, not set
    auto notices = threadNotices(setup, engines, machine);
    REQUIRE(notices.size() == 1);
    CHECK(contains(notices[0].text, "Alpha"));
    CHECK(contains(notices[0].text, "16 threads"));
    REQUIRE(notices[0].fixes.size() == 2);
    CHECK(notices[0].fixes[0].kind == Fix::Kind::LowerEngineThreads);
    CHECK(notices[0].fixes[0].engine == "Alpha");
    CHECK(notices[0].fixes[0].value == 1);
    CHECK(notices[0].fixes[1].kind == Fix::Kind::OpenEngine);
    CHECK(threadNotice(setup, 0, engines, machine).has_value());
    CHECK_FALSE(threadNotice(setup, 1, engines, machine).has_value());

    // One game at a time: half the hardware threads is the limit, and what Lower Threads gives.
    setup.concurrency = 1;
    notices           = threadNotices(setup, engines, machine);
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].fixes[0].value == 8);
    engines[0].setOption("Threads", "8");
    CHECK(threadNotices(setup, engines, machine).empty());

    // The setup gives the Threads: no longer the engine's own.
    setup.concurrency = 7;
    engines[0].clearOption("Threads");
    setup.participants[0].threads = 1;
    CHECK(threadNotices(setup, engines, machine).empty());
}

TEST_CASE("threads: Threads given by the setup beyond the machine, with Lower Threads where they are given") {
    const CpuTopology machine{16, 8};
    std::vector<EngineEntry> engines{engine("Alpha"), engine("Beta"), engine("Gamma")};
    auto setup                    = roundRobin({"Alpha", "Beta", "Gamma"});
    setup.concurrency             = 4;
    setup.participants[1].threads = 4;

    auto notices = threadNotices(setup, engines, machine);
    REQUIRE(notices.size() == 1);
    CHECK(contains(notices[0].text, "Beta"));
    REQUIRE(notices[0].fixes.size() == 1);
    CHECK(notices[0].fixes[0].kind == Fix::Kind::LowerParticipantThreads);
    CHECK(notices[0].fixes[0].index == 1);
    CHECK(notices[0].fixes[0].value == 2);

    // The common Threads: one notice for them all.
    setup.participants[1].threads = 0;
    setup.common_threads          = 6;
    notices                       = threadNotices(setup, engines, machine);
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].fixes[0].kind == Fix::Kind::LowerCommonThreads);

    // Without a clock, the hardware threads count: 4 games × 4 threads fit in 16.
    setup.common_threads = 4;
    setup.limit          = LimitKind::Nodes;
    CHECK(threadNotices(setup, engines, machine).empty());
}

TEST_CASE("threads: a match flags its engines too, one notice for an engine playing both sides") {
    const CpuTopology machine{8, 4};
    std::vector<EngineEntry> engines{engine("Alpha", "8")};
    MatchSetup setup;
    setup.engine_a = setup.engine_b = "Alpha";
    setup.book_a.path               = "a.bin";
    setup.book_b.path               = "b.bin";
    setup.limit                     = LimitKind::Time;
    CHECK(threadNotices(setup, engines, machine).size() == 1);
}

TEST_CASE("programs: an engine whose program or folder is not found, with Open the engine and Locate") {
    std::vector<EngineEntry> engines{engine("Alpha"), engine("Beta")};
    engines[0].program_state = ProgramState::Missing;
    engines[1].program_state = ProgramState::MissingDir;
    engines[1].dir           = "/gone";
    const auto notices       = programNotices(roundRobin({"Alpha", "Beta", "Alpha"}), engines);
    REQUIRE(notices.size() == 2);
    CHECK(notices[0].level == Notice::Level::Error);
    CHECK(contains(notices[0].text, "/engines/Alpha"));
    CHECK(hasFix(notices[0], Fix::Kind::OpenEngine));
    CHECK(hasFix(notices[0], Fix::Kind::LocateProgram));
    CHECK(contains(notices[1].text, "/gone"));
    CHECK(hasFix(notices[1], Fix::Kind::LocateDir));
}

TEST_CASE("options: the values an engine refuses, its own or given by the setup") {
    std::vector<EngineEntry> engines{engine("Alpha"), engine("Beta")};
    engines[0].options   = {{"Hash", "4096"}, {"Style", "Wild"}, {"Gone", "1"}, {"Threads", "2"}};
    auto setup           = roundRobin({"Alpha", "Beta"});
    setup.common_threads = 128;
    const auto notices   = optionNotices(setup, engines);
    // Alpha's Hash and Style, its Gone it does not have; the common Threads both refuse (Alpha's own is replaced).
    REQUIRE(notices.size() == 5);
    CHECK(contains(notices[0].text, "Threads = 128"));
    CHECK(contains(notices[0].text, "1 to 64"));
    CHECK(contains(notices[1].text, "Hash = 4096"));
    CHECK(contains(notices[2].text, "Normal, Risky"));
    CHECK(contains(notices[3].text, "Gone"));
    CHECK(contains(notices[4].text, "Beta"));
    for (const auto& n : notices) CHECK(hasFix(n, Fix::Kind::OpenEngine));

    // An engine not read yet is not judged.
    engines[0].uci_options.clear();
    CHECK(optionNotices(setup, engines).size() == 1);
}

TEST_CASE("memory: the engine with the largest Hash named, Open the engine when it is its own") {
    std::vector<EngineEntry> engines{engine("Alpha"), engine("Beta")};
    engines[1].setOption("Hash", "1024");
    const auto setup = roundRobin({"Alpha", "Beta"});
    MemoryNeed need;
    need.hash_mb   = 4096;
    need.processes = 4;
    need.system    = SystemMemory{2048, 1024};
    auto n         = memoryNotice(setup, engines, need);
    CHECK(n.level == Notice::Level::Error);
    CHECK(contains(n.text, "Beta"));
    CHECK(hasFix(n, Fix::Kind::OpenEngine));

    need.system = SystemMemory{8192, 8192};
    n           = memoryNotice(setup, engines, need);
    CHECK(n.level == Notice::Level::Ok);
    CHECK(n.fixes.empty());
}

TEST_CASE("tablebases: the folders a setup uses, the adjudication's and the engines' SyzygyPath") {
    std::vector<EngineEntry> engines{engine("Alpha"), engine("Beta"), engine("Gamma")};
#ifdef _WIN32
    engines[1].setOption("SyzygyPath", "C:\\tb\\own");
    const std::string tb = "C:\\tb\\345;C:\\tb\\6", own = "C:\\tb\\own";
#else
    engines[1].setOption("SyzygyPath", "/tb/own");
    const std::string tb = "/tb/345:/tb/6", own = "/tb/own";
#endif
    auto setup       = roundRobin({"Alpha", "Beta", "Gamma"});
    setup.tb_enabled = true;
    setup.tb_paths   = tb;
    setup.tb_pieces  = 6;
    auto uses        = tablebaseUses(setup, engines);
    REQUIRE(uses.size() == 2);
    CHECK(uses[0] == TablebaseUse{tb, true, 6, {}});
    CHECK(uses[1] == TablebaseUse{own, false, 0, {"Beta"}});

    // Given to the engines that have SyzygyPath: the same folders as the adjudication's.
    setup.engine_syzygy = true;
    uses                = tablebaseUses(setup, engines);
    REQUIRE(uses.size() == 1);
    CHECK(uses[0].engines == std::vector<std::string>{"Alpha", "Beta", "Gamma"});

    // The adjudication on without a folder: it cannot start.
    CHECK_FALSE(adjudicationFolderNotice(setup).has_value());
    auto none     = setup;
    none.tb_paths = "";
    REQUIRE(adjudicationFolderNotice(none).has_value());
    CHECK(adjudicationFolderNotice(none)->level == Notice::Level::Error);
    CHECK(hasFix(*adjudicationFolderNotice(none), Fix::Kind::LocateTablebases));

    // A relative folder is the engine's working directory's.
    setup.engine_syzygy = setup.tb_enabled = false;
    engines[1].setOption("SyzygyPath", "tb");
    engines[1].dir = (fs::path("/engines") / "beta").string();
    uses           = tablebaseUses(setup, engines);
    REQUIRE(uses.size() == 1);
    CHECK(uses[0].paths == (fs::path(engines[1].dir) / "tb").lexically_normal().string());
}

TEST_CASE("tablebases: what is wrong with the files, said precisely, with the fixes") {
    TablebaseUse adjudication{"/tb", true, 6, {}};
    TablebaseCheck check;
    check.wdl_files = 145;
    check.largest   = 5;

    // Up to 6 pieces asked, 5 found: Lower to 5 pieces.
    auto notices = tablebaseNotices(adjudication, check);
    REQUIRE(notices.size() == 1);
    CHECK(contains(notices[0].text, "6 pieces"));
    CHECK(contains(notices[0].text, "5-piece"));
    CHECK(notices[0].level == Notice::Level::Warning);
    REQUIRE(notices[0].fixes.size() == 2);
    CHECK(notices[0].fixes[0] == lowerTablebasePiecesFix(5));
    CHECK(notices[0].fixes[1].kind == Fix::Kind::OpenFolder);

    // Complete up to 5, as many as asked: all is well.
    adjudication.pieces = 5;
    notices             = tablebaseNotices(adjudication, check);
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].level == Notice::Level::Ok);

    // 6-piece files missing, the adjudication up to what the tables cover: the missing ones named, Lower to 5.
    adjudication.pieces = 0;
    check.largest       = 6;
    check.missing[6]    = {"KRPPvKR", "KRPPvKB"};
    notices             = tablebaseNotices(adjudication, check);
    REQUIRE(notices.size() == 1);
    CHECK(contains(notices[0].text, "Syzygy 6 pieces: 2 files missing in /tb (KRPPvKR.rtbw, KRPPvKB.rtbw)"));
    CHECK(notices[0].fixes[0] == lowerTablebasePiecesFix(5));

    // 5-piece files missing too: Lower to 4, where the tables are complete, on the first incomplete count. A long
    // folder is named by its last two folders, whole in the tooltip.
    check.missing[5]   = {"KBNvKR"};
    const auto folder  = (fs::path("/home") / "tester" / "chess" / "tablebases" / "syzygy" / "6-men").string();
    adjudication.paths = folder;
    notices            = tablebaseNotices(adjudication, check);
    REQUIRE(notices.size() == 2);
    CHECK(notices[0].fixes[0] == lowerTablebasePiecesFix(4));
    CHECK(notices[1].fixes[0].kind == Fix::Kind::OpenFolder);
    CHECK(contains(notices[0].text, (fs::path("…") / "syzygy" / "6-men").string()));
    CHECK(contains(notices[0].detail, folder));
    check.missing.erase(5);

    // An engine's folder: no Lower, but Open the engine; damaged files named.
    TablebaseUse engine_use{"/own", false, 0, {"Alpha"}};
    check.damaged = {"KQvK.rtbz"};
    notices       = tablebaseNotices(engine_use, check);
    REQUIRE(notices.size() == 2);
    CHECK_FALSE(hasFix(notices[0], Fix::Kind::LowerTablebasePieces));
    CHECK(hasFix(notices[0], Fix::Kind::OpenEngine));
    CHECK(contains(notices[1].text, "KQvK.rtbz"));

    // A folder without a tablebase file: a warning for an engine, an error for the adjudication (fastchess does not
    // start it), with Locate.
    TablebaseCheck empty;
    notices = tablebaseNotices(engine_use, empty);
    REQUIRE(notices.size() == 1);
    CHECK(contains(notices[0].text, "SyzygyPath of Alpha points to a folder with no tablebase files"));
    CHECK(notices[0].level == Notice::Level::Warning);
    notices = tablebaseNotices(adjudication, empty);
    REQUIRE(notices.size() == 1);
    CHECK(notices[0].level == Notice::Level::Error);
    CHECK(hasFix(notices[0], Fix::Kind::LocateTablebases));

    // Not found.
    empty.missing_dirs = {"/own"};
    notices            = tablebaseNotices(engine_use, empty);
    REQUIRE(notices.size() == 1);
    CHECK(contains(notices[0].text, "folder not found"));
    CHECK_FALSE(hasFix(notices[0], Fix::Kind::OpenFolder));
}

TEST_CASE("opening book: given and not found, with Locate") {
    MatchSetup setup;
    CHECK_FALSE(bookNotice(setup).has_value());
    setup.book   = (fs::temp_directory_path() / "fcg-no-such-book.epd").string();
    const auto n = bookNotice(setup);
    REQUIRE(n.has_value());
    CHECK(contains(n->text, "fcg-no-such-book.epd"));
    CHECK(hasFix(*n, Fix::Kind::LocateBook));
}

TEST_CASE("the engine that stopped a tournament: the last startup failure fastchess gave up on, by its name") {
    CHECK_FALSE(startupFailure({}).has_value());
    const std::vector<std::string> log = {
        "Started: Gauntlet, 10+0.1",
        "Warning; Stockfish 17 engine startup failure: \"Couldn't start\", trying again in 10 s",
        "Fatal; Old Engine engine startup failure: \"timeout\"",
        "Fatal; Stockfish 17 engine startup failure: \"Couldn't start the engine: No such file\"",
        "Finished",
    };
    const auto failed = startupFailure(log);
    REQUIRE(failed.has_value());
    CHECK(failed->engine == "Stockfish 17");
    CHECK(failed->reason == "Couldn't start the engine: No such file");

    // A participant with a book of its own plays under another name than its engine's.
    std::vector<EngineEntry> engines{engine("Alpha"), engine("Alpha (dev)")};
    CHECK(libraryEngine(engines, "Alpha") == "Alpha");
    CHECK(libraryEngine(engines, "Alpha (dev)") == "Alpha (dev)");
    CHECK(libraryEngine(engines, "Alpha (book.bin)") == "Alpha");
    CHECK(libraryEngine(engines, "Gone (x)").empty());
}

TEST_CASE("the log's lines since a mark: what fastchess said during a tournament") {
    auto& console   = Console::instance();
    const auto mark = console.added();
    console.add("Fatal; Alpha engine startup failure: \"x\"", true);
    console.add("Finished");
    CHECK(console.since(mark) == std::vector<std::string>{"Fatal; Alpha engine startup failure: \"x\"", "Finished"});
    CHECK(console.since(console.added()).empty());
    CHECK(startupFailure(console.since(mark))->engine == "Alpha");
}
