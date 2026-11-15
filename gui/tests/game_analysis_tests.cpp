// The analysis of a game move by move: the winning chances, the judgements and their thresholds, the counts, the
// positions where the game ended, and the analysis kept with the game.

#include <doctest/doctest.hpp>

#include <filesystem>

#include "board_page.hpp"
#include "board_text.hpp"
#include "game_analysis.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

fastchess::Score cp(int64_t v) { return {fastchess::ScoreType::CP, v}; }
fastchess::Score mate(int64_t v) { return {fastchess::ScoreType::MATE, v}; }

ReplayGame gameOf(const char* moves) {
    std::string error;
    auto text = readBoardText(moves);
    REQUIRE(text.kind == BoardText::Kind::Games);
    auto game = readPgnGame(text.pgn, error);
    REQUIRE(game);
    return *game;
}

}  // namespace

TEST_CASE("game analysis: winning chances and the thresholds of the judgements") {
    CHECK(winChance(cp(0)) == doctest::Approx(50.0));
    CHECK(winChance(cp(100)) == doctest::Approx(59.1).epsilon(0.01));
    CHECK(winChance(cp(-100)) == doctest::Approx(100.0 - winChance(cp(100))));
    CHECK(winChance(cp(1000)) > 97.0);
    CHECK(winChance(mate(3)) == 100.0);
    CHECK(winChance(mate(-3)) == 0.0);
    CHECK(winChance(mate(0)) == 0.0);  // mated

    // A loss of 10 points of winning chances is an inaccuracy, of 20 a mistake, of 30 a blunder.
    CHECK(judge(60, 51) == Judgement::None);
    CHECK(judge(60, 50) == Judgement::Inaccuracy);
    CHECK(judge(60, 40.5) == Judgement::Inaccuracy);
    CHECK(judge(60, 40) == Judgement::Mistake);
    CHECK(judge(60, 30.5) == Judgement::Mistake);
    CHECK(judge(60, 30) == Judgement::Blunder);
    CHECK(judge(40, 80) == Judgement::None);  // better than the engine thought
    CHECK(std::string(judgementMark(Judgement::Blunder)) == "??");
    CHECK(std::string(judgementMark(Judgement::Mistake)) == "?");
    CHECK(std::string(judgementMark(Judgement::Inaccuracy)) == "?!");
    CHECK(std::string(judgementMark(Judgement::None)).empty());
}

TEST_CASE("game analysis: each move judged from the positions before and after it, counted by side") {
    // 1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7#
    const auto game = gameOf("e4 e5 Qh5 Nc6 Bc4 Nf6 Qxf7#");
    REQUIRE(game.moves.size() == 7);

    GameReview review;
    // Scores of the side to move in each position (8 of them), and the engine's best moves.
    review.positions = {{cp(30), "e2e4"}, {cp(-25), "e7e5"},  {cp(30), "g1f3"},  {cp(-20), "b8c6"},
                        {cp(40), "f1c4"}, {cp(-900), "g7g6"}, {mate(1), "h5f7"}, {mate(0), ""}};
    const auto moves = reviewMoves(game, false, review);
    REQUIRE(moves.size() == 7);
    CHECK(moves[0].best);  // e4: the engine's own choice, not judged
    CHECK(moves[0].judgement == Judgement::None);
    CHECK(moves[2].judgement == Judgement::None);  // Qh5: 30 to 20 for White, a small loss
    CHECK_FALSE(moves[2].best);
    CHECK(moves[2].loss == doctest::Approx(winChance(cp(30)) - winChance(cp(20))));
    // 3... Nf6?? lets the mate in: from -900 (Black's view, at its move)... Black's chances before Nf6 are those of
    // -900, after it 0 (mated in one).
    CHECK(moves[5].judgement == Judgement::None);  // already lost: a loss of a few points only
    // Qxf7#: the best move, mating.
    CHECK(moves[6].best);

    // Bc4 from +40 to an equal position: White's chances from 53.7 to 50, nothing to say.
    review.positions[5] = {cp(0), "g8f6"};
    review.positions[6] = {cp(500), "h5f7"};
    auto judged         = reviewMoves(game, false, review);
    CHECK(judged[4].judgement == Judgement::None);
    CHECK(judged[4].loss == doctest::Approx(winChance(cp(40)) - 50.0));
    // Nf6 is the engine's move there: not judged, whatever it lost.
    CHECK(judged[5].best);
    CHECK(judged[5].judgement == Judgement::None);
    // A blunder: Black was about equal (51), and its move gave White +500 (Black's chances 14).
    review.positions[5] = {cp(10), "g7g6"};
    judged              = reviewMoves(game, false, review);
    CHECK(judged[5].judgement == Judgement::Blunder);
    CHECK(judged[5].loss == doctest::Approx(winChance(cp(10)) - (100 - winChance(cp(500)))));
    const auto counts = judgementCounts(game, judged);
    CHECK(counts[1][2] == 1);  // one blunder for Black
    CHECK(counts[0][2] == 0);

    // Fewer positions than moves (a review cancelled): only those judged.
    review.positions.resize(3);
    judged = reviewMoves(game, false, review);
    CHECK(judged.size() == 7);
    CHECK(judged[3].judgement == Judgement::None);
    CHECK_FALSE(review.complete(game.moves.size()));
}

TEST_CASE("game analysis: the end of the game without an engine, the analysis kept with the game") {
    const auto game = gameOf("f3 e5 g4 Qh4#");
    CHECK(endScore(boardAt(game, false, 4))->type == fastchess::ScoreType::MATE);
    CHECK(endScore(boardAt(game, false, 4))->value == 0);
    CHECK_FALSE(endScore(boardAt(game, false, 3)).has_value());
    chess::Board stalemate("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    CHECK(endScore(stalemate)->type == fastchess::ScoreType::CP);
    CHECK(endScore(stalemate)->value == 0);

    // Kept under a key of the start position and the moves.
    const auto other = gameOf("f3 e5 g4 Qh4");
    CHECK(reviewKey(game) == reviewKey(other));  // the same moves (the check sign aside)
    CHECK(reviewKey(game) != reviewKey(gameOf("f3 e5 g4 d5")));
    CHECK(reviewKey(game).size() == 16);

    const auto dir = fs::temp_directory_path() / ("fcg-review-tests-" + reviewKey(game));
    fs::remove_all(dir);
    CHECK_FALSE(loadReview(dir, game));
    GameReview review;
    review.engine      = "Bravo";
    review.movetime_ms = 500;
    review.positions   = {
        {cp(20), "e2e4", 18}, {cp(-150), "d7d5", 17}, {cp(300), "e1f2", 16}, {mate(1), "d8h4", 30}, {mate(0), "", 0}};
    REQUIRE(saveReview(dir, game, review));
    const auto loaded = loadReview(dir, game);
    REQUIRE(loaded);
    CHECK(loaded->engine == "Bravo");
    CHECK(loaded->movetime_ms == 500);
    REQUIRE(loaded->positions.size() == 5);
    CHECK(loaded->positions[1].score->value == -150);
    CHECK(loaded->positions[3].score->type == fastchess::ScoreType::MATE);
    CHECK(loaded->positions[2].best == "e1f2");
    CHECK(loaded->positions[0].depth == 18);
    CHECK(loaded->complete(game.moves.size()));
    CHECK_FALSE(loadReview(dir, gameOf("e4 e5")));
    fs::remove_all(dir);
}
