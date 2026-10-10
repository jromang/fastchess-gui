#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <core/config/config.hpp>
#include <core/globals/globals.hpp>
#include <core/observer.hpp>
#include <game/pgn/pgn_builder.hpp>
#include <matchmaking/elo/elo_pentanomial.hpp>
#include <matchmaking/sprt/sprt.hpp>

#include "engines.hpp"
#include "pgn_store.hpp"
#include "run.hpp"
#include "stats.hpp"
#include "topology.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

fs::path tempDir(const char* name) {
    auto dir = fs::temp_directory_path() /
               fmt::format("fcg-tests-{}-{}", name, std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(dir);
    return dir;
}

// A small finished game: 1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7#, with scores from the mover's point of view.
fastchess::MatchData sampleGame(bool timed) {
    fastchess::MatchData data(chess::constants::STARTPOS);

    const char* moves[]   = {"e2e4", "e7e5", "d1h5", "b8c6", "f1c4", "g8f6", "h5f7"};
    const int64_t cp[]    = {35, -20, 50, -80, 150, -300, 0};
    const bool mate[]     = {false, false, false, false, false, false, true};
    const int64_t mates[] = {0, 0, 0, 0, 0, 0, 1};

    for (int i = 0; i < 7; ++i) {
        fastchess::MoveData m(moves[i]);
        m.depth          = 10 + i;
        m.elapsed_millis = 1234 + i;
        m.timeleft       = 59000 - i * 1000;
        m.score          = mate[i] ? fastchess::Score{fastchess::ScoreType::MATE, mates[i]}
                                   : fastchess::Score{fastchess::ScoreType::CP, cp[i]};
        data.moves.push_back(m);
    }

    data.players.white.config.name = "A";
    data.players.black.config.name = "B";
    if (timed) {
        data.players.white.config.limit.tc.time = 60000;
        data.players.black.config.limit.tc.time = 60000;
    }
    data.players.white.result = chess::GameResult::WIN;
    data.players.black.result = chess::GameResult::LOSE;
    data.termination          = fastchess::MatchTermination::NORMAL;
    data.reason               = "White mates";
    return data;
}

}  // namespace

TEST_CASE("topology: physical cores are distinct (package, core) pairs") {
    std::vector<std::pair<int, int>> cpus;
    for (int smt = 0; smt < 2; ++smt) {
        for (int core = 0; core < 16; ++core) cpus.emplace_back(0, core);
    }

    const auto t = topologyFromCores(cpus);
    CHECK(t.logical == 32);
    CHECK(t.physical == 16);
    CHECK(t.defaultConcurrency(true) == 32);
    CHECK(t.defaultConcurrency(false) == 15);

    // Two sockets reuse core ids.
    const auto two = topologyFromCores({{0, 0}, {0, 1}, {1, 0}, {1, 1}});
    CHECK(two.physical == 4);

    CHECK(topologyFromCores({{0, 0}}).defaultConcurrency(false) == 1);
    CHECK(detectTopology().logical >= 1);
}

TEST_CASE("uci option lines") {
    auto spin = parseUciOption("option name Hash type spin default 16 min 1 max 33554432");
    REQUIRE(spin);
    CHECK(spin->name == "Hash");
    CHECK(spin->type == "spin");
    CHECK(spin->default_value == "16");
    CHECK(spin->min == "1");
    CHECK(spin->max == "33554432");

    auto combo = parseUciOption("option name Numa Policy type combo default auto var auto var none var hardware");
    REQUIRE(combo);
    CHECK(combo->name == "Numa Policy");
    CHECK(combo->vars == std::vector<std::string>{"auto", "none", "hardware"});

    auto str = parseUciOption("option name SyzygyPath type string default <empty>");
    REQUIRE(str);
    CHECK(str->default_value.empty());

    auto spaced = parseUciOption("option name Debug Log File type string default C:/my logs/a b.txt");
    REQUIRE(spaced);
    CHECK(spaced->name == "Debug Log File");
    CHECK(spaced->default_value == "C:/my logs/a b.txt");

    auto button = parseUciOption("option name Clear Hash type button");
    REQUIRE(button);
    CHECK(button->type == "button");

    CHECK_FALSE(parseUciOption("id name Foo"));
    CHECK_FALSE(parseUciOption("option name type spin"));
}

TEST_CASE("pruneOptions keeps only values that differ from the engine default") {
    EngineEntry e;
    e.uci_options = {{"Hash", "spin", "16", "1", "1024", {}}, {"Clear Hash", "button", "", "", "", {}}};
    e.options     = {{"Hash", "16"}, {"Clear Hash", "false"}, {"Gone", "1"}};
    pruneOptions(e);
    CHECK(e.options.empty());

    e.options = {{"Hash", "64"}, {"Clear Hash", "true"}};
    pruneOptions(e);
    CHECK(e.options.size() == 2);
    CHECK(e.threads() == 1);
}

TEST_CASE("ChessBase comments give White's point of view, clocks and move times") {
    fastchess::config::Pgn config;
    const auto data = sampleGame(true);
    const auto pgn  = fastchess::pgn::PgnBuilder(config, data, 1, fastchess::pgn::CommentStyle::ChessBase).get();

    CHECK(pgn.find("1. e4 {[%eval 0.35,10] [%clk 0:00:59.000] [%emt 0:00:01.234]}") != std::string::npos);
    // Black's -0.20 is +0.20 for White.
    CHECK(pgn.find("e5 {[%eval 0.20,11] [%clk 0:00:58.000] [%emt 0:00:01.235]}") != std::string::npos);
    CHECK(pgn.find("Nf6 {[%eval 3.00,15]") != std::string::npos);
    CHECK(pgn.find("Qxf7# {[%eval #1,16]") != std::string::npos);
    CHECK(pgn.find("White mates}") != std::string::npos);

    // Without a clock there is no %clk.
    const auto untimed =
        fastchess::pgn::PgnBuilder(config, sampleGame(false), 1, fastchess::pgn::CommentStyle::ChessBase).get();
    CHECK(untimed.find("%clk") == std::string::npos);
    CHECK(untimed.find("[%emt 0:00:01.234]") != std::string::npos);

    // The default style is untouched.
    const auto classic = fastchess::pgn::PgnBuilder(config, data, 1).get();
    CHECK(classic.find("1. e4 {+0.35/10 1.234s}") != std::string::npos);
    CHECK(classic.find("e5 {-0.20/11 1.235s}") != std::string::npos);
}

TEST_CASE("fastchess comments parse back") {
    ReplayMove m;
    parseFastchessComment("+0.35/18 1.234s, n=1000", m);
    REQUIRE(m.score);
    CHECK(m.score->type == fastchess::ScoreType::CP);
    CHECK(m.score->value == 35);
    CHECK(m.depth == 18);
    CHECK(m.time_ms == 1234);

    ReplayMove neg;
    parseFastchessComment("-1.07/9 0.050s", neg);
    CHECK(neg.score->value == -107);
    CHECK(neg.time_ms == 50);

    // fastchess prints mates in plies.
    ReplayMove mate;
    parseFastchessComment("+M5/30 0.1s", mate);
    CHECK(mate.score->type == fastchess::ScoreType::MATE);
    CHECK(mate.score->value == 3);

    ReplayMove mated;
    parseFastchessComment("-M4/30 0.1s", mated);
    CHECK(mated.score->value == -2);

    ReplayMove zero;
    parseFastchessComment("0.00/12 0.5s", zero);
    CHECK(zero.score->value == 0);

    ReplayMove book;
    parseFastchessComment("book", book);
    CHECK(book.book);
}

TEST_CASE("pgn file offsets read games back, and replay parses them") {
    const auto dir = tempDir("pgn");
    {
        PgnFile file(dir / "games.pgn");

        fastchess::config::Pgn config;
        const auto first  = fastchess::pgn::PgnBuilder(config, sampleGame(true), 1).get();
        const auto second = fastchess::pgn::PgnBuilder(config, sampleGame(false), 2).get();

        const auto s1 = file.append(first);
        const auto s2 = file.append(second);
        CHECK(s1.offset == 0);
        CHECK(s2.offset == first.size());

        REQUIRE(file.read(s2));
        CHECK(*file.read(s2) == second);

        const auto replay = parseReplay(*file.read(s1));
        REQUIRE(replay);
        REQUIRE(replay->moves.size() == 7);
        CHECK(replay->moves[0].san == "e4");
        CHECK(replay->moves[0].score->value == 35);
        CHECK(replay->moves[1].mover == chess::Color::BLACK);
        CHECK(replay->moves[1].score->value == -20);
        CHECK(replay->moves[6].san == "Qxf7#");
        CHECK(replay->moves[6].score->type == fastchess::ScoreType::MATE);
        CHECK(replay->result == "1-0");
        CHECK(replay->header("White") == "A");
        CHECK(replay->final_comment == "White mates");

        const auto out = dir / "export.pgn";
        CHECK(exportSpans(file.path(), {s2}, out).empty());
        std::ifstream in(out, std::ios::binary);
        const std::string exported((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(exported == second);
    }

    // Windows refuses to delete open files, hence the scope above.
    fs::remove_all(dir);
}

TEST_CASE("pause gate holds new pairs and lets running games and their pair finish") {
    using fastchess::observer::PauseGate;
    auto& gate = PauseGate::instance();

    fastchess::atomic::stop = false;
    gate.reset();

    {
        const auto running = gate.enter(1, 2);
        CHECK(gate.running() == 1);

        gate.setPaused(true);
        std::atomic_bool passed{false};
        // The second game of pair 1 may still start: its partner is running.
        {
            const auto partner = gate.enter(1, 2);
            CHECK(gate.running() == 2);
        }

        std::thread waiter([&] {
            const auto ticket = gate.enter(2, 2);
            passed            = true;
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        CHECK_FALSE(passed);
        CHECK(gate.running() == 1);  // the game that was already running is still counted

        gate.setPaused(false);
        waiter.join();
        CHECK(passed);
    }
    CHECK(gate.running() == 0);

    // Stopping releases a paused gate.
    gate.setPaused(true);
    std::atomic_bool released{false};
    std::thread waiter([&] {
        const auto ticket = gate.enter(3, 2);
        released          = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK_FALSE(released);
    fastchess::atomic::stop = true;
    gate.wakeAll();
    waiter.join();
    CHECK(released);

    fastchess::atomic::stop = false;
    gate.reset();
}

TEST_CASE("displayed statistics are fastchess' own") {
    fastchess::Stats stats(3, 20, 15, 40, 18, 4);  // LL LD WL DD WD WW
    stats.wins   = 90;
    stats.losses = 70;
    stats.draws  = 40;

    const fastchess::SPRT sprt(0.05, 0.05, 0.0, 5.0, "normalized", true);
    const auto p = computeStats(stats, true, &sprt);

    const fastchess::elo::EloPentanomial reference(stats);
    CHECK(p.valid);
    CHECK(p.elo == doctest::Approx(reference.diff()));
    CHECK(p.elo_err == doctest::Approx(reference.error()));
    CHECK(p.nelo == doctest::Approx(reference.nEloDiff()));
    CHECK(p.llr == doctest::Approx(sprt.getLLR(stats, true)));
    CHECK(p.games == 200);

    // No result yet, or no variance: nothing to show, but no NaN either.
    CHECK_FALSE(computeStats(fastchess::Stats{}, true, &sprt).valid);
    fastchess::Stats draws;
    draws.draws    = 4;
    draws.penta_DD = 2;
    CHECK_FALSE(computeStats(draws, true, &sprt).valid);
}

TEST_CASE("the setup becomes a sanitized fastchess configuration") {
    const auto dir = tempDir("run");
    // fastchess appends .exe to extension-less commands on Windows.
    const auto binary = dir / "engine.exe";
    std::ofstream(binary) << "x";

    std::vector<EngineEntry> engines(2);
    engines[0].name    = "new";
    engines[0].cmd     = binary.string();
    engines[1].name    = "old";
    engines[1].cmd     = binary.string();
    engines[1].options = {{"Hash", "64"}};

    MatchSetup setup;
    setup.engine_a    = "new";
    setup.engine_b    = "old";
    setup.mode        = MatchMode::Fixed;
    setup.fixed_games = 101;
    setup.limit       = LimitKind::Nodes;
    setup.nodes       = 5000;

    const auto topology = topologyFromCores({{0, 0}, {0, 1}, {0, 0}, {0, 1}});

    auto run = buildRun(setup, engines, topology, dir);
    CHECK(run.tournament.rounds == 51);
    CHECK(run.tournament.games == 2);
    CHECK(run.tournament.concurrency == 4);  // every hardware thread at fixed nodes
    CHECK(run.engines[0].name == "new");
    CHECK(run.engines[0].limit.nodes == 5000);
    CHECK(run.engines[0].dir == dir.string());
    CHECK(run.engines[1].options.size() == 1);
    CHECK(run.tournament.pgn.file.empty());
    CHECK(fs::path(run.tournament.config_name).is_absolute());
    CHECK(run.info.limit_text == "5000 nodes");
    CHECK(run.info.total_games == 102);

    setup.limit     = LimitKind::Time;
    setup.tc_base_s = 10;
    setup.tc_inc_s  = 0.1;
    setup.mode      = MatchMode::Sprt;
    run             = buildRun(setup, engines, topology, dir);
    CHECK(run.tournament.concurrency == 1);  // physical cores - 1
    CHECK(run.engines[1].limit.tc.time == 10000);
    CHECK(run.engines[1].limit.tc.increment == 100);
    CHECK(run.engines[1].limit.nodes == 0);
    CHECK(run.tournament.sprt.enabled);
    CHECK(run.info.total_games == 0);
    CHECK(run.info.limit_text == "10+0.1");

    // A multi-threaded engine turns affinity off.
    engines[0].options = {{"Threads", "4"}};
    CHECK_FALSE(buildRun(setup, engines, topology, dir).tournament.affinity);
    engines[0].options.clear();
    CHECK(buildRun(setup, engines, topology, dir).tournament.affinity);

    setup.engine_b = "missing";
    CHECK_THROWS((void)buildRun(setup, engines, topology, dir));

    fs::remove_all(dir);
}
