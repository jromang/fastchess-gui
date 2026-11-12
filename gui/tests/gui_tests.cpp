#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

#include <core/config/config.hpp>
#include <core/globals/globals.hpp>
#include <core/observer.hpp>
#include <engine/uci_engine.hpp>
#include <game/pgn/pgn_builder.hpp>
#include <matchmaking/elo/elo_pentanomial.hpp>
#include <matchmaking/match/match.hpp>
#include <matchmaking/sprt/sprt.hpp>

#include "app.hpp"
#include "board_view.hpp"
#include "cpu_time.hpp"
#include "engine_import.hpp"
#include "engines.hpp"
#include "live.hpp"
#include "pgn_store.hpp"
#include "run.hpp"
#include "stats.hpp"
#include "storage.hpp"
#include "topology.hpp"
#include "ui.hpp"

#ifdef _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif

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

TEST_CASE("UCI_ShowWDL is switched on for a new engine that has it, and left as it was set") {
    EngineEntry e;
    showWdlByDefault(e);
    CHECK(e.options.empty());
    e.uci_options = {{"UCI_ShowWDL", "check", "false", "", "", {}}};
    showWdlByDefault(e);
    REQUIRE(e.options.size() == 1);
    CHECK(e.options[0] == std::pair<std::string, std::string>{"UCI_ShowWDL", "true"});
    e.options[0].second = "false";
    showWdlByDefault(e);
    CHECK(e.options.size() == 1);
    CHECK(e.options[0].second == "false");
}

TEST_CASE("an engine's program: taken as it is, then noticed when rebuilt, moved, or without its folder") {
    const auto dir     = tempDir("program");
    const auto program = dir / "engine";
    std::ofstream(program) << "build 1";

    EngineEntry e;
    e.cmd = program.string();
    CHECK(refreshProgram(e));  // fingerprinted, to be saved
    CHECK(e.program_state == ProgramState::Ok);
    CHECK(e.program.size == 7);
    CHECK_FALSE(refreshProgram(e));

    // Copied over with the same contents: still the same program, under a new date.
    fs::last_write_time(program, fs::last_write_time(program) + std::chrono::seconds(5));
    CHECK(refreshProgram(e));
    CHECK(e.program_state == ProgramState::Ok);

    std::ofstream(program) << "build 2";
    CHECK_FALSE(refreshProgram(e));
    CHECK(e.program_state == ProgramState::Changed);

    e.dir = (dir / "gone").string();
    (void)refreshProgram(e);
    CHECK(e.program_state == ProgramState::MissingDir);

    fs::remove(program);
    (void)refreshProgram(e);
    CHECK(e.program_state == ProgramState::Missing);
}

TEST_CASE("an engine's program is fingerprinted in two parts, its CRC read only when the file system cannot tell") {
    const auto dir     = tempDir("program-crc");
    const auto program = dir / "engine";
    std::ofstream(program) << "build 1";

    EngineEntry e;
    e.cmd = program.string();
    REQUIRE(programNeedsCrc(e));  // never fingerprinted
    CHECK(applyFingerprint(e, tour::fingerprint(e.cmd)));
    CHECK(e.program_state == ProgramState::Ok);
    CHECK_FALSE(programNeedsCrc(e));  // same size, same date

    // Another size: rebuilt, without reading it.
    std::ofstream(program) << "build 10";
    CHECK_FALSE(programNeedsCrc(e));
    CHECK(e.program_state == ProgramState::Changed);

    // The same size under another date: only the CRC tells.
    std::ofstream(program) << "build 1";
    fs::last_write_time(program, fs::last_write_time(program) + std::chrono::seconds(5));
    e.program_state = ProgramState::Unknown;
    REQUIRE(programNeedsCrc(e));
    CHECK(applyFingerprint(e, tour::fingerprint(e.cmd)));  // the new date, to save
    CHECK(e.program_state == ProgramState::Ok);
    std::ofstream(program) << "build 2";
    fs::last_write_time(program, fs::last_write_time(program) + std::chrono::seconds(10));
    REQUIRE(programNeedsCrc(e));
    CHECK_FALSE(applyFingerprint(e, tour::fingerprint(e.cmd)));
    CHECK(e.program_state == ProgramState::Changed);
    fs::remove_all(dir);
}

TEST_CASE("openings: PGN tags, and the Games filter by ECO code, range or words of the name") {
    const std::string pgn =
        "[Event \"x\"]\n[ECO \"B90\"]\n[Opening \"Sicilian Defense: Najdorf Variation\"]\n\n"
        "1. e4 {[ECO \"A00\"]} c5 1-0\n";
    CHECK(pgnTag(pgn, "ECO") == "B90");
    CHECK(pgnTag(pgn, "Opening") == "Sicilian Defense: Najdorf Variation");
    CHECK(pgnTag(pgn, "EC").empty());
    CHECK(pgnTag(pgn, "Round").empty());

    const std::string eco = "B90", name = "Sicilian Defense: Najdorf Variation";
    CHECK(openingMatches(eco, name, ""));
    CHECK(openingMatches(eco, name, "B"));
    CHECK(openingMatches(eco, name, "b9"));
    CHECK(openingMatches(eco, name, "B90"));
    CHECK_FALSE(openingMatches(eco, name, "B91"));
    CHECK(openingMatches(eco, name, "B20-B99"));
    CHECK(openingMatches(eco, name, "b20 - b99"));
    CHECK_FALSE(openingMatches(eco, name, "C00-C99"));
    CHECK(openingMatches(eco, name, "najdorf sicilian"));
    CHECK_FALSE(openingMatches(eco, name, "sicilian dragon"));
}

TEST_CASE("evaluation facts: a disagreement, a swing, the loser's best score, and where they happen") {
    const std::string pgn =
        "[Event \"x\"]\n[Result \"0-1\"]\n\n"
        "1. e4 {book} e5 {book} 2. Nf3 {+0.30/10 0.1s} Nc6 {+2.50/10 0.1s} 3. Bb5 {+0.40/10 0.1s} "
        "a6 {+2.60/10 0.1s} 4. Ba4 {+0.50/10 0.1s} Nf6 {+2.70/10 0.1s} 5. O-O {+6.00/10 0.1s} "
        "Be7 {+M3/12 0.1s} 0-1\n";
    const auto facts = evalFacts(pgn, -1);
    CHECK(facts.disagreement == doctest::Approx(3.0f));  // plies 4 to 9: 3.0, 3.1, 3.2, 8.7, 16
    CHECK(facts.disagree_ply == 4);
    CHECK(facts.swing == doctest::Approx(7.3f));  // Black: -2.7 for White, then a mate, -10
    CHECK(facts.swing_ply == 9);
    CHECK(facts.loser_best == doctest::Approx(6.0f));
    CHECK(facts.loser_ply == 8);

    const auto draw = evalFacts(pgn, 0);
    CHECK(draw.loser_ply == -1);
}

TEST_CASE("processor use: our own process is measured; too many cores or thinking on the other's time is told") {
#ifdef _WIN32
    const auto self = static_cast<int64_t>(GetCurrentProcessId());
#else
    const auto self = static_cast<int64_t>(getpid());
#endif
    const auto tree = processTree(self);
    REQUIRE_FALSE(tree.empty());
    CHECK(tree.front() == self);
    const auto before = cpuTimeMs(tree);
    REQUIRE(before >= 0);
    volatile uint64_t x = 0;
    const auto end      = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < end) x = x + 1;
    CHECK(cpuTimeMs(tree) >= before + 100);
    CHECK(cpuTimeMs({}) == -1);

    EngineTotals t;
    t.own_wall_ms = t.wait_wall_ms = 60'000;
    t.own_cpu_ms                   = 60'000;  // one core while it thinks
    CHECK(cpuProblem(t, 1).empty());
    t.own_cpu_ms = 180'000;  // three
    CHECK(cpuProblem(t, 1).find("3.0 cores") != std::string::npos);
    CHECK(cpuProblem(t, 4).empty());
    t.own_cpu_ms  = 60'000;
    t.wait_cpu_ms = 30'000;  // half a core while waiting
    CHECK(cpuProblem(t, 1).find("50 %") != std::string::npos);
    t.wait_wall_ms = 5'000;  // too little measured to tell
    CHECK(cpuProblem(t, 1).empty());
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

    // WDL, read from the info line, written when asked and known.
    const auto info = fastchess::engine::UciEngine::parseInfo(
        "info depth 10 seldepth 14 score cp 30 wdl 120 830 50 nodes 1000 pv e2e4 e7e5");
    REQUIRE(info.wdl);
    CHECK(*info.wdl == std::array<int64_t, 3>{120, 830, 50});
    CHECK(info.nodes == 1000u);
    CHECK_FALSE(fastchess::engine::UciEngine::parseInfo("info depth 10 score cp 30 wdl 120 830").wdl);
    auto with_wdl         = data;
    with_wdl.moves[0].wdl = info.wdl;
    config.track_wdl      = true;
    const auto wdl        = fastchess::pgn::PgnBuilder(config, with_wdl, 1).get();
    CHECK(wdl.find("1. e4 {+0.35/10 1.234s, wdl=120/830/50}") != std::string::npos);
    CHECK(wdl.find("e5 {-0.20/11 1.235s}") != std::string::npos);
    const auto wdl_cb = fastchess::pgn::PgnBuilder(config, with_wdl, 1, fastchess::pgn::CommentStyle::ChessBase).get();
    CHECK(wdl_cb.find("[%emt 0:00:01.234], wdl=120/830/50}") != std::string::npos);
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

    ReplayMove wdl;
    parseFastchessComment("+0.35/18 1.234s, nps=900000, wdl=120/830/50", wdl);
    REQUIRE(wdl.wdl);
    CHECK(*wdl.wdl == std::array<int64_t, 3>{120, 830, 50});
    CHECK(wdl.nps == 900000);
    ReplayMove cut;
    parseFastchessComment("+0.35/18 1.234s, wdl=120/830", cut);
    CHECK_FALSE(cut.wdl);
}

TEST_CASE("WDL: from the mover's point of view to White's, after the plies that have it") {
    std::vector<ReplayMove> moves(3);
    moves[0].book  = true;
    moves[1].mover = chess::Color::BLACK;
    moves[1].wdl   = std::array<int64_t, 3>{600, 300, 100};  // Black wins 60 %
    moves[2].wdl   = std::array<int64_t, 3>{200, 700, 100};
    const auto s   = wdlSeries(moves);
    REQUIRE(s.ply.size() == 2);
    CHECK(s.ply[0] == 2);
    CHECK(s.win[0] == doctest::Approx(0.1));
    CHECK(s.not_lost[0] == doctest::Approx(0.4));
    CHECK(s.win[1] == doctest::Approx(0.2));
    CHECK(s.not_lost[1] == doctest::Approx(0.9));
}

TEST_CASE("board details: the 50-move counter, the material difference and the pieces left") {
    chess::Board board;
    auto info = boardInfo(board);
    CHECK(info.halfmoves == 0);
    CHECK(info.pieces == 32);
    CHECK(info.balance == 0);

    // Three pawns and a rook against a queen, a knight each.
    board.setFen("4k3/1q6/8/8/3n4/8/PPP5/1R2K1N1 w - - 37 60");
    info = boardInfo(board);
    CHECK(info.halfmoves == 37);
    CHECK(info.pieces == 9);
    CHECK(info.surplus == std::array<int, 5>{3, 0, 0, 1, -1});
    CHECK(info.balance == 3 + 5 - 9);
}

TEST_CASE("tablebase adjudication: its own limit, or the largest tables in its folders") {
    namespace fs   = std::filesystem;
    const auto dir = fs::temp_directory_path() / "fcg-gui-syzygy";
    fs::remove_all(dir);
    fs::create_directories(dir / "a");
    fs::create_directories(dir / "b");
    for (const auto* f : {"a/KQvK.rtbw", "a/KQvK.rtbz", "b/KRPvKR.rtbw", "b/KRPPvKRP.rtbz", "b/readme.txt"}) {
        std::ofstream(dir / f) << "x";
    }
    fastchess::config::TbAdjudication tb;
    tb.syzygy_dirs = (dir / "a").string();
    CHECK(tablebasePieces(tb) == 0);  // off
    tb.enabled = true;
    CHECK(tablebasePieces(tb) == 3);
#ifdef _WIN32
    tb.syzygy_dirs += ";" + (dir / "b").string();
#else
    tb.syzygy_dirs += ":" + (dir / "b").string();
#endif
    CHECK(tablebasePieces(tb) == 5);  // the WDL tables decide, not the larger DTZ one
    tb.max_pieces = 4;
    CHECK(tablebasePieces(tb) == 4);
    fs::remove_all(dir);
}

TEST_CASE("a Chess960 game replays, castling included") {
    const auto game = parseReplay(R"([White "A"]
[Black "B"]
[Result "*"]
[SetUp "1"]
[FEN "brnbnkqr/pppppppp/8/8/8/8/PPPPPPPP/BRNBNKQR w HBhb - 0 1"]
[Variant "Chess960"]

1. c3 f6 2. b3 c5 3. Bc2 Ned6 4. c4 Kf7 5. d4 cxd4 6. f3 e5 7. e3 Qe8 8. exd4 Ne7 9. Ne2 b5 10. c5 Nb7
11. dxe5 Nxc5 12. Qxc5 Bd5 13. exf6 Rc8 14. Qxa7 Rxc2 15. Nxc2 Bxf3 16. gxf3 gxf6 17. h4 Nf5 18. O-O-O Be7 *
)");
    REQUIRE(game);
    REQUIRE(game->moves.size() == 36);
    const auto& castle = game->moves[34];
    CHECK(castle.san == "O-O-O");
    CHECK(castle.move.typeOf() == chess::Move::CASTLING);
    CHECK(chess::uci::moveToUci(castle.move, true) == "f1b1");  // king takes the b1 rook
}

TEST_CASE("pgn file offsets read games back, and replay parses them") {
    const auto dir = tempDir("pgn");
    {
        tour::PgnFile file(dir / "games.pgn");

        fastchess::config::Pgn config;
        const auto first  = fastchess::pgn::PgnBuilder(config, sampleGame(true), 1).get();
        const auto second = fastchess::pgn::PgnBuilder(config, sampleGame(false), 2).get();

        const auto s1 = file.append(first);
        const auto s2 = file.append(second);
        CHECK(s1.offset == 0);
        CHECK(s2.offset == first.size());

        REQUIRE(tour::readSpan(file.path(), s2));
        CHECK(*tour::readSpan(file.path(), s2) == second);

        const auto replay = parseReplay(*tour::readSpan(file.path(), s1));
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
    engines[1].elo     = 3200;

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
    CHECK(run.tournament.startup_time == ENGINE_STARTUP_TIME);
    CHECK(run.tournament.concurrency == 4);  // every hardware thread at fixed nodes
    CHECK(run.engines[0].name == "new");
    CHECK(run.engines[0].limit.nodes == 5000);
    CHECK(run.engines[0].dir == dir.string());
    CHECK(run.engines[1].options.size() == 1);
    CHECK(run.tournament.pgn.file.empty());
    CHECK(run.tournament.config_name.empty());  // results are kept by the tournament layer
    CHECK(run.spec.format == tour::Format::Match);
    CHECK(run.spec.max_games == 102);
    REQUIRE(run.spec.participants.size() == 2);
    CHECK(run.spec.participants[1].engine.name == "old");
    CHECK(run.spec.participants[1].elo == 3200);
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

    // A time per move, and a depth: like a node count, it does not depend on the machine's speed.
    setup.limit        = LimitKind::MoveTime;
    setup.move_s       = 0.5;
    setup.tc_margin_ms = 50;
    run                = buildRun(setup, engines, topology, dir);
    CHECK(run.engines[0].limit.tc.fixed_time == 500);
    CHECK(run.engines[0].limit.tc.timemargin == 50);
    CHECK(run.engines[0].limit.tc.time == 0);
    CHECK(run.info.limit == LimitKind::MoveTime);
    CHECK(run.info.limit_text == "0.5 s/move");
    setup.limit = LimitKind::Depth;
    setup.depth = 12;
    run         = buildRun(setup, engines, topology, dir);
    CHECK(run.engines[1].limit.plies == 12);
    CHECK(run.engines[1].limit.tc.fixed_time == 0);
    CHECK(run.tournament.concurrency == 4);
    CHECK(run.info.limit == LimitKind::Depth);
    CHECK(run.info.limit_text == "depth 12");
    setup.depth = 0;
    CHECK_THROWS((void)buildRun(setup, engines, topology, dir));
    setup.limit        = LimitKind::Time;
    setup.tc_margin_ms = 0;

    // Affinity is off by default; a multi-threaded engine turns it off when asked.
    CHECK_FALSE(buildRun(setup, engines, topology, dir).tournament.affinity);
    setup.affinity     = true;
    engines[0].options = {{"Threads", "4"}};
    CHECK_FALSE(buildRun(setup, engines, topology, dir).tournament.affinity);
    engines[0].options.clear();
    CHECK(buildRun(setup, engines, topology, dir).tournament.affinity);

    setup.engine_b = "missing";
    CHECK_THROWS((void)buildRun(setup, engines, topology, dir));

    // A round robin: common Hash, an engine's own Threads, time odds; the engine without a Hash option keeps none.
    engines[0].uci_options = {{"Hash", "spin", "16", "1", "1024"}, {"Threads", "spin", "1", "1", "64"}};
    engines[1].uci_options = {{"Threads", "spin", "1", "1", "64"}};
    setup.format           = TournamentFormat::RoundRobin;
    setup.participants     = {{"new", 0, 4, 50}, {"old"}};
    setup.common_hash      = 256;
    setup.cycles           = 3;
    setup.limit            = LimitKind::Time;
    setup.tc_base_s        = 60;
    setup.tc_inc_s         = 1;
    run                    = buildRun(setup, engines, topology, dir);
    REQUIRE(run.engines.size() == 2);
    CHECK(run.spec.format == tour::Format::RoundRobin);
    CHECK(run.spec.cycles == 3);
    CHECK_FALSE(run.tournament.sprt.enabled);
    CHECK(run.info.total_games == 6);
    const auto option = [](const fastchess::EngineConfiguration& c, const std::string& name) -> std::string {
        for (const auto& [k, v] : c.options) {
            if (k == name) return v;
        }
        return "";
    };
    CHECK(option(run.engines[0], "Hash") == "256");
    CHECK(option(run.engines[0], "Threads") == "4");
    CHECK(option(run.engines[1], "Hash") == "64");  // its own value: it has no Hash option to force
    CHECK(run.engines[0].limit.tc.time == 30000);
    CHECK(run.engines[0].limit.tc.increment == 500);
    CHECK(run.engines[1].limit.tc.time == 60000);

    // Chess960: every engine plays it, the 960 positions when there is no book.
    setup.chess960 = true;
    CHECK_THROWS((void)buildRun(setup, engines, topology, dir));  // neither has UCI_Chess960
    engines[0].uci_options.push_back({"UCI_Chess960", "check", "false"});
    engines[1].uci_options.push_back({"UCI_Chess960", "check", "false"});
    run = buildRun(setup, engines, topology, dir);
    CHECK(run.tournament.variant == fastchess::VariantType::FRC);
    CHECK(run.engines[0].variant == fastchess::VariantType::FRC);
    CHECK(fs::path(run.tournament.opening.file).filename() == "chess960.epd");
    CHECK(fs::is_regular_file(run.tournament.opening.file));
    setup.chess960 = false;

    setup.participants = {{"new"}, {"new"}};
    CHECK_THROWS((void)buildRun(setup, engines, topology, dir));
    setup.participants = {{"new"}};
    CHECK_THROWS((void)buildRun(setup, engines, topology, dir));

    // A Polyglot book: its lines become positions in the tournament folder.
    setup.participants = {{"new"}, {"old"}};
    {
        const auto bin = dir / "tiny.bin";
        std::ofstream out(bin, std::ios::binary);
        const auto put = [&](uint64_t v, int bytes) {
            for (int i = bytes - 1; i >= 0; --i) out.put(static_cast<char>((v >> (8 * i)) & 0xff));
        };
        for (const uint16_t move : {uint16_t(0x031c), uint16_t(0x02db)}) {  // e2e4, d2d4
            put(chess::Board().hash(), 8);
            put(move, 2);
            put(10, 2);
            put(0, 4);
        }
        out.close();
        setup.book       = bin.string();
        setup.book_plies = 1;
        run              = buildRun(setup, engines, topology, dir / "polyglot");
        CHECK(run.tournament.opening.format == fastchess::FormatType::EPD);
        CHECK(fs::path(run.tournament.opening.file) == dir / "polyglot" / "tiny-1plies.epd");
        std::ifstream epd(run.tournament.opening.file);
        int lines = 0;
        for (std::string line; std::getline(epd, line);) ++lines;
        CHECK(lines == 2);
        setup.book.clear();
        setup.book_plies = -1;
    }

    // A program that is not there any more.
    engines[1].cmd = (dir / "moved.exe").string();
    CHECK_THROWS_WITH((void)buildRun(setup, engines, topology, dir),
                      doctest::Contains("The program of old is not found"));

    fs::remove_all(dir);
}

TEST_CASE("the 960 Chess960 starting positions: 518 is the usual one, all different and legal") {
    CHECK(chess960Fen(518) == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    CHECK(chess960Fen(0) == "bbqnnrkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w KQkq - 0 1");
    std::set<std::string> seen;
    for (int n = 0; n < 960; ++n) {
        const auto fen  = chess960Fen(n);
        const auto rank = fen.substr(fen.rfind('/') + 1, 8);
        seen.insert(rank);
        // Bishops on squares of both colours, the king between the rooks.
        const auto b1 = rank.find('B'), b2 = rank.rfind('B');
        CHECK((b1 + b2) % 2 == 1);
        const auto r1 = rank.find('R'), k = rank.find('K'), r2 = rank.rfind('R');
        CHECK((r1 < k && k < r2));
        chess::Board board(fen, true);
        CHECK(board.getFen(false).substr(0, 8) == fen.substr(0, 8));
    }
    CHECK(seen.size() == 960);
}

TEST_CASE("engine lists of ChessGUI, cutechess and Arena are imported") {
    const auto dir = tempDir("import");
    fs::create_directories(dir / "engines" / "fruit");
    std::ofstream(dir / "engines" / "fruit" / "fruit.exe") << "x";

    // ChessGUI: ChsGUI_Eng.TXT and the UCI options in ChsGUI_Cfg.ini, Windows paths.
    std::ofstream(dir / "ChsGUI_Eng.TXT")
        << "[Fruit 2.1]\r\nMyName=Fruit 2.1\r\nEngineFile=engines\\fruit\\fruit.exe\r\n"
           "CommandLine=\r\nEngineCode=1 0 0\r\n\r\n[Human_1]\r\nMyName=Human_1\r\n";
    std::ofstream(dir / "ChsGUI_Cfg.ini") << "[Fruit 2.1]\r\nHash=64\r\nOwnBook=false\r\n";
    auto result = importEngineList(dir / "ChsGUI_Eng.TXT");
    REQUIRE(result.engines.size() == 1);
    CHECK(result.engines[0].name == "Fruit 2.1");
    CHECK(result.engines[0].protocol.empty());  // ChessGUI does not say: the probe finds out
    CHECK(fs::path(result.engines[0].cmd) == (dir / "engines" / "fruit" / "fruit.exe").lexically_normal());
    REQUIRE(result.engines[0].options.size() == 2);
    CHECK(result.engines[0].options[0] == std::make_pair(std::string("Hash"), std::string("64")));

    // cutechess: engines.json, with a WinBoard engine.
    std::ofstream(dir / "engines.json") << R"([
        {"name": "Fruit", "command": "fruit.exe", "workingDirectory": "engines/fruit", "protocol": "uci",
         "options": [{"name": "Hash", "type": "spin", "default": 16, "value": 128}]},
        {"name": "Crafty", "command": "crafty", "protocol": "xboard"}])";
    result = importEngineList(dir / "engines.json");
    REQUIRE(result.engines.size() == 2);
    CHECK(result.engines[0].name == "Fruit");
    CHECK(result.engines[0].protocol == "uci");
    CHECK(fs::path(result.engines[0].cmd).filename() == "fruit.exe");
    REQUIRE(result.engines[0].options.size() == 1);
    CHECK(result.engines[0].options[0].second == "128");
    CHECK(result.engines[1].name == "Crafty");
    CHECK(result.engines[1].protocol == "xboard");
    // The JSON lists too say which programs are not there.
    REQUIRE(result.notes.size() == 1);
    CHECK(result.notes[0].find("Crafty") != std::string::npos);

    // An INI list with Command= and Type= (Arena style).
    std::ofstream(dir / "arena.cfg")
        << "[Fruit]\nCommand=engines/fruit/fruit.exe\nType=UCI\n[Old]\nCommand=old.exe\nType=Winboard\n";
    result = importEngineList(dir / "arena.cfg");
    REQUIRE(result.engines.size() == 2);
    CHECK(result.engines[0].name == "Fruit");
    CHECK(result.engines[0].protocol == "uci");
    CHECK(result.engines[1].protocol == "xboard");

    fs::remove_all(dir);
}

TEST_CASE("string options that hold a file or a folder are told by their name") {
    const auto kind = [](const char* name, const char* type = "string") {
        UciOptionInfo o;
        o.name = name;
        o.type = type;
        return optionPath(o);
    };
    CHECK(kind("EvalFile") == OptionPath::File);
    CHECK(kind("BookFile") == OptionPath::File);
    CHECK(kind("WeightsFile") == OptionPath::File);
    CHECK(kind("SyzygyPath") == OptionPath::Folder);
    CHECK(kind("NalimovPath") == OptionPath::Folder);
    CHECK(kind("Book Path") == OptionPath::Folder);
    CHECK(kind("UCI_Opponent") == OptionPath::None);
    CHECK(kind("OwnBook", "check") == OptionPath::None);
}

TEST_CASE("an engine imported again is known by its program and arguments, not by its options") {
    EngineEntry known;
    known.cmd     = "/engines/sf/stockfish";
    known.options = {{"UCI_ShowWDL", "true"}};  // what its first read added
    EngineEntry again;
    again.cmd     = "/engines/sf/stockfish";
    again.options = {{"Hash", "16"}};
    CHECK(sameEngine(known, again));
    again.args = "--large-pages";
    CHECK_FALSE(sameEngine(known, again));
    again.args = "";
    again.cmd  = "/engines/SF/Stockfish";
#ifdef _WIN32
    CHECK(sameEngine(known, again));
#else
    CHECK_FALSE(sameEngine(known, again));
#endif
}

TEST_CASE("a folder is opened by a file URL that spaces, accents and # cannot break") {
    CHECK(ui::fileUrl("/home/jeff/runs/2026_RR") == "file:///home/jeff/runs/2026_RR");
    CHECK(ui::fileUrl(std::filesystem::path(u8"/home/Hélène/Mes tournois #2")) ==
          "file:///home/H%C3%A9l%C3%A8ne/Mes%20tournois%20%232");
    CHECK(ui::fileUrl("C:/Users/x") == "file:///C:/Users/x");
}

TEST_CASE("text that is not UTF-8 is read as Windows-1252, and never stops engines.json from being written") {
    namespace fs = std::filesystem;
    CHECK(toUtf8("G\xC3\xA9za") == "G\xC3\xA9za");                    // already UTF-8
    CHECK(toUtf8("G\xE9za") == "G\xC3\xA9za");                        // Latin-1
    CHECK(toUtf8("\x80 \x99") == "\xE2\x82\xAC \xE2\x84\xA2");        // Windows-1252: euro, trade mark
    CHECK(toUtf8("\xC0\xAF") == "\xC3\x80\xC2\xAF");                  // an overlong form is not UTF-8
    CHECK(toUtf8("\xED\xA0\x80") == "\xC3\xAD\xC2\xA0\xE2\x82\xAC");  // nor a surrogate
    CHECK(toUtf8("caf\xC3") == "caf\xC3\x83");                        // nor a sequence cut short

    // An engine list written by a Windows program, accents in Windows-1252.
    const auto dir = fs::temp_directory_path() / "fcg-utf8-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "arena.cfg") << "[R\xE9sum\xE9]\nCommand=engines/r\xE9sum\xE9.exe\nType=UCI\n";
    const auto imported = importEngineList(dir / "arena.cfg");
    REQUIRE(imported.engines.size() == 1);
    CHECK(imported.engines[0].name == "R\xC3\xA9sum\xC3\xA9");

    // Whatever an engine wrote is saved, with U+FFFD for what is not UTF-8.
    Storage storage(dir / "profile");
    EngineEntry e;
    e.name      = "Old";
    e.id_author = "G\xE9za";
    storage.saveEngines({e});
    const auto engines = storage.loadEngines();
    REQUIRE(engines.size() == 1);
    CHECK(engines[0].id_author == "G\xEF\xBF\xBDza");
    fs::remove_all(dir);
}

TEST_CASE("the tournaments folder is kept in settings.json, the default restored by an empty path") {
    namespace fs     = std::filesystem;
    const auto root  = fs::temp_directory_path() / "fcg-storage-test";
    const auto other = fs::temp_directory_path() / "fcg-storage-test-elsewhere";
    fs::remove_all(root);

    {
        Storage storage(root);
        CHECK(storage.runsDir() == root / "runs");
        storage.setRunsDir(other);
        CHECK(storage.runsDir() == other);
    }
    Storage reopened(root);
    CHECK(reopened.runsDir() == other);  // headless mode reads it too

    reopened.setRunsDir({});
    CHECK(reopened.runsDir() == root / "runs");
    CHECK(Storage(root).runsDir() == root / "runs");
}

TEST_CASE("an engine list that cannot be read is set aside, not replaced by an empty one") {
    namespace fs    = std::filesystem;
    const auto root = fs::temp_directory_path() / "fcg-storage-unreadable";
    fs::remove_all(root);
    fs::create_directories(root);
    std::ofstream(root / "engines.json") << "[{\"name\": \"Stockfish\", ";  // cut short

    Storage storage(root);
    CHECK(storage.loadEngines().empty());
    REQUIRE(fs::exists(root / "engines.json.unreadable"));
    storage.saveEngines({});  // what quitting does
    {
        std::ifstream in(root / "engines.json.unreadable");  // closed before the folder is removed (Windows)
        CHECK(std::string(std::istreambuf_iterator<char>(in), {}).find("Stockfish") != std::string::npos);
    }

    // A second unreadable list does not replace the first one set aside.
    std::ofstream(root / "engines.json") << "garbage";
    CHECK(storage.loadEngines().empty());
    CHECK(fs::exists(root / "engines.json.unreadable2"));
    fs::remove_all(root);
}

TEST_CASE("a settings.json of another shape is taken as empty, at start and when a setting is saved") {
    namespace fs    = std::filesystem;
    const auto root = fs::temp_directory_path() / "fcg-storage-settings";
    for (const char* text : {"[]", "null", "\"x\"", "{\"tournaments_dir\": 5, \"lichess_token\": null}"}) {
        CAPTURE(text);
        fs::remove_all(root);
        fs::create_directories(root);
        std::ofstream(root / "settings.json") << text;
        Storage storage(root);
        CHECK(storage.runsDir() == root / "runs");
        CHECK(storage.lichessToken().empty());
        CHECK(storage.ratingPgns().empty());
        storage.setLichessToken("token");
        CHECK(storage.lichessToken() == "token");
    }
    fs::remove_all(root);
}

#ifndef _WIN32
TEST_CASE("checking an engine in a folder that cannot be searched says so, it does not throw") {
    namespace fs   = std::filesystem;
    const auto dir = fs::temp_directory_path() / "fcg-check-locked";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::permissions(dir, fs::perms::none);
    EngineEntry entry;
    entry.cmd = (dir / "engine").string();
    CheckResult result;
    CHECK_NOTHROW(result = checkEngine(entry));
    CHECK_FALSE(result.ok);
    fs::permissions(dir, fs::perms::owner_all);
    fs::remove_all(dir);
}
#endif

TEST_CASE("names that cannot be file names are made fit for one") {
    CHECK(cleanName("Hiarcs \xDC 15/x:y") == "Hiarcs___15_x_y");
    CHECK(cleanName("Stockfish-17.1_dev") == "Stockfish-17.1_dev");
}

TEST_CASE("search statistics are read back from the move comments, without replaying the moves") {
    fastchess::config::Pgn config;
    config.track_nps = true;
    auto data        = sampleGame(true);
    for (std::size_t i = 0; i < data.moves.size(); ++i) data.moves[i].nps = 1000 * (i + 1);
    const auto pgn = fastchess::pgn::PgnBuilder(config, data, 1).get();

    int white = 0, black = 0, depth = 0;
    int64_t time = 0, nps = 0;
    forEachMoveComment(pgn, [&](bool white_moved, const ReplayMove& move) {
        (white_moved ? white : black)++;
        depth += move.depth;
        time += move.time_ms;
        nps += move.nps;
    });
    CHECK(white == 4);
    CHECK(black == 3);
    CHECK(depth == 10 + 11 + 12 + 13 + 14 + 15 + 16);
    CHECK(time == 7 * 1234 + 21);
    CHECK(nps == 1000 * 28);

    // Black first from a FEN.
    const auto fen = "[FEN \"8/8/8/8/8/8/8/k6K b - - 0 1\"]\n\n1... Ka2 {+0.00/5 0.100s} 2. Kh2 {+0.00/6 0.200s}";
    std::vector<bool> movers;
    forEachMoveComment(fen, [&](bool white_moved, const ReplayMove&) { movers.push_back(white_moved); });
    CHECK(movers == std::vector<bool>{false, true});
}

TEST_CASE("SPRT calculator: fishtest's pass probabilities and expected games") {
    // Reference values from fishtest's own sprt.js.
    const auto normalized = sprtPlan(0.05, 0.05, 0, 2, "normalized", 0.49, 191);
    REQUIRE(normalized.valid);
    CHECK(normalized.elo0 == doctest::Approx(0.0));
    CHECK(normalized.elo1 == doctest::Approx(1.019165186));
    CHECK(normalized.at(normalized.elo0).pass == doctest::Approx(0.05));
    CHECK(normalized.at(normalized.elo1).pass == doctest::Approx(0.95));
    CHECK(normalized.at(normalized.elo1).games == doctest::Approx(159942.4222));
    CHECK(normalized.at(1).pass == doctest::Approx(0.9444699991));
    CHECK(normalized.at(1).games == doctest::Approx(164150.5039));
    CHECK(normalized.at(-1).pass == doctest::Approx(0.0001628401842).epsilon(1e-6));
    CHECK(normalized.at(-1).games == doctest::Approx(59970.45852));

    const auto logistic = sprtPlan(0.05, 0.05, 0, 3, "logistic", 0.6, 0);
    REQUIRE(logistic.valid);
    CHECK(logistic.elo1 == doctest::Approx(3.0));
    CHECK(logistic.at(1).pass == doctest::Approx(0.2726068841));
    CHECK(logistic.at(1).games == doctest::Approx(43109.00216));
    CHECK(logistic.at(0).games == doctest::Approx(28435.62173));

    const auto regression = sprtPlan(0.05, 0.05, -1.75, 0.25, "normalized", 0.49, 191);
    CHECK(regression.elo0 == doctest::Approx(-0.8917689382));
    CHECK(regression.at(0).pass == doctest::Approx(0.9009949893));
    CHECK(regression.at(0).games == doctest::Approx(190032.9181));

    // BayesElo: bounds in BayesElo, larger than logistic ones for the same scores.
    const auto bayes = sprtPlan(0.05, 0.05, 0, 5, "bayesian", 0.5, 0);
    REQUIRE(bayes.valid);
    CHECK(bayes.elo1 > 0);
    CHECK(bayes.elo1 < 5);
    CHECK(bayes.at(bayes.elo1).pass == doctest::Approx(0.95));

    // A bias the draw ratio cannot hold, and bounds in the wrong order.
    CHECK_FALSE(sprtPlan(0.05, 0.05, 0, 2, "normalized", 0.9, 191).valid);
    CHECK_FALSE(sprtPlan(0.05, 0.05, 2, 0, "logistic", 0.5, 0).valid);
}

TEST_CASE("match statistics: fishtest's numbers, from the games and from the pairs") {
    fastchess::Stats s(120, 80, 600);
    s.penta_LL = 2, s.penta_LD = 50, s.penta_WL = 30, s.penta_DD = 250, s.penta_WD = 60, s.penta_WW = 8;
    const auto m = matchStatistics(s, nullptr);

    // Reference values from fishtest's formulas (template_helpers.py).
    CHECK(m.games.count == 800);
    CHECK(m.games.draw_ratio == doctest::Approx(0.75));
    CHECK(m.games.score.value == doctest::Approx(0.525));
    CHECK(m.games.score.low == doctest::Approx(0.5077630388));
    CHECK(m.games.stdev == doctest::Approx(0.2487468593));
    CHECK(m.games.nelo.value == doctest::Approx(34.9185901817));
    CHECK(m.games.nelo.low == doctest::Approx(10.8777251228));
    CHECK(m.games.nelo.high == doctest::Approx(58.9594552406));
    CHECK(m.pairs.count == 400);
    CHECK(m.pairs.draw_ratio == doctest::Approx(0.7));
    CHECK(m.pairs.score.value == doctest::Approx(0.51375));
    CHECK(m.pairs.stdev == doctest::Approx(0.2156313405));
    CHECK(m.pairs.nelo.value == doctest::Approx(22.1546612388));
    CHECK(m.pairs.nelo.low == doctest::Approx(-1.6100760439));
    CHECK(m.variance_ratio == doctest::Approx(0.7514646465));
    CHECK(m.rms_bias == doctest::Approx(88.0050419444));
    CHECK(m.bayeselo == doctest::Approx(40.1829685561));
    CHECK(m.drawelo == doctest::Approx(341.5140352196));
    CHECK(m.pairs_ratio == doctest::Approx(1.3076923077));
    CHECK(m.two_draws == doctest::Approx(0.625));
    CHECK(m.win_and_loss == doctest::Approx(0.075));
    CHECK(std::isnan(m.games.llr));

    // The LLR of both counts with the match's SPRT.
    const fastchess::SPRT sprt(0.05, 0.05, 0, 5, "normalized", true);
    const auto with_sprt = matchStatistics(s, &sprt);
    CHECK(with_sprt.games.llr == doctest::Approx(sprt.getLLR(s, false)));
    CHECK(with_sprt.pairs.llr == doctest::Approx(sprt.getLLR(s, true)));
    CHECK(statisticsText(with_sprt).find("RMS bias: 88.0 Elo") != std::string::npos);

    // Nothing played: nothing defined, nothing thrown.
    const auto empty = matchStatistics(fastchess::Stats{}, nullptr);
    CHECK(empty.games.count == 0);
    CHECK(std::isnan(empty.games.nelo.value));
    CHECK(std::isnan(empty.rms_bias));
}

TEST_CASE("decision time: when the first PV move last changed, from the lines of the search") {
    using fastchess::decisionTime;
    const std::vector<std::string_view> lines = {
        "info depth 1 time 5 score cp 10 pv e2e4",
        "info depth 2 time 20 score cp 15 pv d2d4 d7d5",
        "info depth 3 multipv 2 time 30 score cp 5 pv e2e4",    // not the main line
        "info depth 3 time 35 score cp 40 lowerbound pv c2c4",  // a bound, not a PV yet
        "info depth 3 time 50 score cp 16 pv d2d4 d7d5 c2c4",
        "info depth 4 time 90 score cp 17 pv d2d4",
    };
    CHECK(decisionTime(lines, "d2d4", 100) == 20);
    CHECK(decisionTime(lines, "e2e4", 100) == 100);  // played against its last PV: decided at the end
    CHECK_FALSE(decisionTime({"info depth 1 score cp 10 pv e2e4"}, "e2e4", 100));  // no time in the lines
    CHECK_FALSE(decisionTime(lines, "", 100));

    ReplayMove m;
    parseFastchessComment("+0.17/4 0.100s, n=12345, sd=9, tbhits=7, pv=\"d2d4 d7d5\", dt=0.020s", m);
    CHECK(m.nodes == 12345u);
    CHECK(m.seldepth == 9);
    CHECK(m.tbhits == 7u);
    CHECK(m.pv == "d2d4 d7d5");
    CHECK(m.decision_ms == 20);
    CHECK(m.time_ms == 100);
}

TEST_CASE("move charts: each side's values at its plies, and how far the PVs agree") {
    CHECK(pvAgreement("e2e4 e7e5 g1f3 b8c6", "e7e5 g1f3 g8f6") == 2);
    CHECK(pvAgreement("e2e4 e7e5", "c7c5") == 0);
    CHECK(pvAgreement("", "e7e5") == 0);

    std::vector<LiveMove> moves(4);
    moves[0].book = true;
    for (std::size_t i = 1; i < moves.size(); ++i) {
        moves[i].mover   = i % 2 ? chess::Color::BLACK : chess::Color::WHITE;
        moves[i].time_ms = static_cast<int64_t>(i) * 1000;
        moves[i].depth   = static_cast<int64_t>(10 + i);
    }
    moves[1].decision_ms = 250;
    moves[1].pv          = "e7e5 g1f3 b8c6";
    moves[2].pv          = "g1f3 b8c6 f1b5";
    moves[3].pv          = "b8c6 a2a3";

    const auto time = moveSeries(moves, MoveChart::Time);
    REQUIRE(time.ply[1].size() == 2);  // Black's moves: plies 2 and 4
    CHECK(time.ply[1][0] == 2);
    CHECK(time.value[1][0] == doctest::Approx(1.0));
    CHECK(time.extra[1][0] == doctest::Approx(0.25));
    CHECK(std::isnan(time.extra[1][1]));
    CHECK(time.ply[0].size() == 1);

    const auto agreement = moveSeries(moves, MoveChart::Agreement);
    REQUIRE(agreement.ply[0].size() == 1);
    CHECK(agreement.value[0][0] == 2);  // White's g1f3 b8c6 after Black's e7e5 g1f3 b8c6
    CHECK(agreement.value[1][0] == 1);
    CHECK(moveSeries(moves, MoveChart::TbHits).empty());
    CHECK(moveSeries(moves, MoveChart::Eval).empty());
}
