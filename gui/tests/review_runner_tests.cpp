// The continuous analysis of a game: its passes, the principal variation replayed, the analyses kept with the game
// (several, and as the first versions wrote one), and the runner against the mock engine.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

#include "../../tournament/tests/self_path.hpp"
#include "game_analysis.hpp"
#include "review_runner.hpp"
#include "storage.hpp"

using namespace fcg;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

std::string randomMover() { return (fcg::test::selfPath().parent_path() / FCG_RANDOM_MOVER).string(); }

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

struct ScopedEnv {
    const char* name;
    ScopedEnv(const char* n, const char* value) : name(n) { setEnv(n, value); }
    ~ScopedEnv() { setEnv(name, nullptr); }
};

ReplayGame shortGame() {
    ReplayGame game;
    chess::Board board;
    for (const char* uci : {"e2e4", "e7e5", "g1f3", "b8c6"}) {
        ReplayMove m;
        m.move  = chess::uci::uciToMove(board, uci);
        m.san   = chess::uci::moveToSan(board, m.move);
        m.mover = board.sideToMove();
        board.makeMove(m.move);
        game.moves.push_back(m);
    }
    return game;
}

fastchess::Score cp(int64_t v) { return {fastchess::ScoreType::CP, v}; }

}  // namespace

TEST_CASE("continuous analysis: four times longer at each pass, as many passes as the ceiling allows") {
    CHECK(passTime(0) == FIRST_PASS_MS);
    CHECK(passTime(1) == 4 * FIRST_PASS_MS);
    CHECK(passTime(3) == 64 * FIRST_PASS_MS);
    CHECK(reviewPasses(passTime(2)) == 3);
    CHECK(reviewPasses(passTime(2) + 1) == 3);
    CHECK(reviewPasses(5120) == 5);
    CHECK(reviewPasses(1) == 1);   // always one pass
    CHECK(reviewPasses(0) == 13);  // until stopped
}

TEST_CASE("continuous analysis: the positions a principal variation goes through, legal and before the end") {
    const AnalysisPosition start{std::string(chess::constants::STARTPOS), {"e2e4"}, false};
    // Its last move left out: nothing searched after it.
    auto line = pvPositions(start, "e7e5 g1f3 b8c6", 8);
    REQUIRE(line.size() == 2);
    CHECK(line[0].moves == std::vector<std::string>{"e2e4", "e7e5"});
    CHECK(line[1].moves == std::vector<std::string>{"e2e4", "e7e5", "g1f3"});
    // At most `max`.
    CHECK(pvPositions(start, "e7e5 g1f3 b8c6 f1b5 a7a6", 2).size() == 2);
    // An illegal move ends it, a move alone gives nothing.
    CHECK(pvPositions(start, "e7e5 e1e3 b8c6", 8).size() == 1);
    CHECK(pvPositions(start, "e7e5", 8).empty());
    // Nothing after a mate: 1.f3 e5 2.g4 Qh4#.
    const AnalysisPosition fools{std::string(chess::constants::STARTPOS), {"f2f3", "e7e5", "g2g4"}, false};
    CHECK(pvPositions(fools, "d8h4 a2a3 a7a6", 8).size() == 1);
}

TEST_CASE("continuous analysis: several analyses kept with a game, and one as the first versions wrote it") {
    const auto dir  = fs::temp_directory_path() / "reviews";
    const auto game = shortGame();
    fs::remove_all(dir);
    CHECK(loadReviews(dir, game).empty());

    GameReview one, two;
    one.engine      = "One";
    one.passes      = 3;
    one.movetime_ms = passTime(2);
    for (int i = 0; i < 5; ++i)
        one.positions.push_back({cp(10 * i), "e2e4", 12, passTime(2), "e2e4 e7e5", std::nullopt});
    one.positions[2].wdl = std::array<int64_t, 3>{300, 600, 100};
    one.previous         = {0, 1, 0, 2};
    two                  = one;
    two.engine           = "Two";
    REQUIRE(saveReviews(dir, game, {one, two}));
    auto loaded = loadReviews(dir, game);
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[0].engine == "One");
    CHECK(loaded[0].passes == 3);
    CHECK(loaded[0].positions[3].time_ms == passTime(2));
    CHECK(loaded[0].positions[3].pv == "e2e4 e7e5");
    REQUIRE(loaded[0].positions[2].wdl);
    CHECK((*loaded[0].positions[2].wdl)[1] == 600);
    CHECK(loaded[0].previous == std::vector<int8_t>{0, 1, 0, 2});
    CHECK(loaded[1].engine == "Two");

    // The same engine's analysis saved again: in its place; another engine's after the others.
    two.passes = 4;
    REQUIRE(saveReview(dir, game, two));
    loaded = loadReviews(dir, game);
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[1].engine == "Two");
    CHECK(loaded[1].passes == 4);
    GameReview three = one;
    three.engine     = "Three";
    REQUIRE(saveReview(dir, game, three));
    loaded = loadReviews(dir, game);
    REQUIRE(loaded.size() == 3);
    CHECK(loaded[0].engine == "One");
    CHECK(loaded[2].engine == "Three");

    // A file of the first versions: one analysis at its top level.
    const auto file = dir / (reviewKey(game) + ".json");
    nlohmann::json old;
    old["key"]         = reviewKey(game);
    old["engine"]      = "Old";
    old["movetime_ms"] = 1000;
    old["depth"]       = 0;
    old["positions"]   = nlohmann::json::array({{{"type", "cp"}, {"value", 20}, {"best", "e2e4"}, {"depth", 10}}});
    REQUIRE(writeJson(file, old));
    loaded = loadReviews(dir, game);
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].engine == "Old");
    CHECK(loaded[0].passes == 0);
    CHECK(loaded[0].positions[0].time_ms == 0);

    REQUIRE(saveReviews(dir, game, {}));
    CHECK_FALSE(fs::exists(file));
    fs::remove_all(dir);
}

TEST_CASE("continuous analysis: the mock engine searches every position at each pass, until the ceiling") {
    if (!fs::is_regular_file(randomMover())) return;
    ScopedEnv env("FCG_MOCK_ANALYSIS", "1");
    const int before = boardThreads();

    ReviewRunner runner;
    ReviewRunner::Setup setup;
    setup.engine.name      = "mover";
    setup.engine.cmd       = randomMover();
    setup.engine.protocol  = "uci";
    setup.settings.threads = 2;
    setup.game             = shortGame();
    setup.ceiling_ms       = passTime(1);  // two passes
    runner.start(setup);

    ReviewRunner::State st;
    uint64_t version = 0;
    const auto until = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < until) {
        (void)runner.state(st, version);
        if (!st.running) break;
        std::this_thread::sleep_for(10ms);
    }
    CHECK(st.error.empty());
    REQUIRE_FALSE(st.running);
    CHECK(st.settled.engine == "mover");
    CHECK(st.settled.passes == 2);
    CHECK(st.settled.movetime_ms == passTime(1));
    REQUIRE(st.settled.positions.size() == 5);
    for (const auto& p : st.settled.positions) {
        CHECK(p.score.has_value());
        CHECK(p.time_ms == passTime(1));
        CHECK_FALSE(p.best.empty());
    }
    // The judgements of the first pass, one per move, to show what the second changed.
    CHECK(st.settled.previous.size() == 4);
    CHECK(st.level == std::vector<int>(5, 1));
    CHECK(runner.threads() == 0);
    CHECK(boardThreads() == before);

    // Resumed after its passes, with a higher ceiling: only the third pass is searched.
    setup.resume     = st.settled;
    setup.ceiling_ms = passTime(2);
    runner.start(setup);
    version = 0;
    while (std::chrono::steady_clock::now() < until + 60s) {
        (void)runner.state(st, version);
        if (!st.running) break;
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE_FALSE(st.running);
    CHECK(st.settled.passes == 3);
    CHECK(st.settled.positions[0].time_ms == passTime(2));
}

TEST_CASE("continuous analysis: stopped, it keeps the passes it completed") {
    if (!fs::is_regular_file(randomMover())) return;
    ScopedEnv env("FCG_MOCK_ANALYSIS", "1");
    ReviewRunner runner;
    ReviewRunner::Setup setup;
    setup.engine.name     = "mover";
    setup.engine.cmd      = randomMover();
    setup.engine.protocol = "uci";
    setup.game            = shortGame();
    setup.ceiling_ms      = 0;  // until stopped
    runner.start(setup);

    // Once the first pass is complete, stopped during a later one.
    ReviewRunner::State st;
    uint64_t version = 0;
    const auto until = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < until) {
        (void)runner.state(st, version);
        if (st.settled.passes >= 1 || !st.running) break;
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(st.settled.passes >= 1);
    runner.stop();
    (void)runner.state(st, version);
    CHECK_FALSE(st.running);
    CHECK(st.error.empty());
    CHECK(st.settled.passes >= 1);
    CHECK(st.settled.positions.size() == 5);
    CHECK(runner.threads() == 0);
}

TEST_CASE("continuous analysis: an analysis waits at a pass for the others, then goes on") {
    if (!fs::is_regular_file(randomMover())) return;
    ScopedEnv env("FCG_MOCK_ANALYSIS", "1");
    ReviewRunner runner;
    ReviewRunner::Setup setup;
    setup.engine.name     = "mover";
    setup.engine.cmd      = randomMover();
    setup.engine.protocol = "uci";
    setup.game            = shortGame();
    setup.ceiling_ms      = passTime(1);
    runner.start(setup);
    runner.setPassLimit(0);

    // Its first pass done, it holds before the second.
    ReviewRunner::State st;
    uint64_t version = 0;
    const auto until = std::chrono::steady_clock::now() + 60s;
    while (std::chrono::steady_clock::now() < until) {
        (void)runner.state(st, version);
        if (st.settled.passes >= 1 || !st.running) break;
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(st.settled.passes == 1);
    CHECK(st.running);
    CHECK(st.pass == 1);
    CHECK(std::count(st.done.begin(), st.done.end(), true) == 0);

    // Let go, it completes its last pass.
    runner.setPassLimit(1);
    while (std::chrono::steady_clock::now() < until) {
        (void)runner.state(st, version);
        if (!st.running) break;
        std::this_thread::sleep_for(10ms);
    }
    CHECK(st.settled.passes == 2);
}
