// The game graph's data: the phases, the engines' curves read from the moves, the analyses, the marks, the time
// strip, the charts the game has values for, and the agreement bars.

#include <doctest/doctest.hpp>

#include <cmath>

#include "board_text.hpp"
#include "game_graph.hpp"

using namespace fcg;

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

// Two book moves, then four with the engines' comments, the PV and the speed on some.
const char* PGN = R"([White "Alpha"]
[Black "Bravo"]
[Result "1-0"]

1. e4 {book} e5 {book} 2. Nf3 {+0.30/12 0.200s, n=1000, sd=20, nps=5000, tbhits=0, pv="g1f3 b8c6 f1b5"}
Nc6 {-0.25/11 0.300s, n=2000, sd=18, nps=6000, tbhits=0, pv="b8c6 f1b5 a7a6"} 3. Bb5 {+0.40/13 0.100s, pv="f1b5 a7a6"}
a6 {-0.50/12 0.150s} 1-0
)";

}  // namespace

TEST_CASE("game graph: phases as lichess divides them") {
    // The start position: no middlegame yet.
    auto phases = gamePhases(gameOf("e4 e5"), false);
    CHECK(phases.middlegame == -1);
    CHECK(phases.endgame == -1);
    CHECK(phaseAt(phases, 2) == Phase::Opening);

    // 11 pieces besides kings and pawns, then a knight taken: 10, the middlegame.
    ReplayGame trade;
    trade.start_fen = "rnbqk2r/pppp1ppp/8/4n3/4P3/5N2/PPPP1PPP/RNBQK3 w Qkq - 0 1";
    trade.moves.push_back({});
    trade.moves.back().move = chess::uci::uciToMove(chess::Board(trade.start_fen), "f3e5");
    phases                  = gamePhases(trade, false);
    CHECK(phases.middlegame == 1);
    CHECK(phases.endgame == -1);
    CHECK(phaseAt(phases, 0) == Phase::Opening);
    CHECK(phaseAt(phases, 1) == Phase::Middlegame);

    // A back rank of 3 pieces: the middlegame, whatever the count.
    ReplayGame sparse;
    sparse.start_fen = "rnbqkbnr/pppppppp/8/8/8/2NBQN2/PPPPPPPP/R3K2R w KQkq - 0 1";
    CHECK(gamePhases(sparse, false).middlegame == 0);

    // A position with 6 pieces besides kings and pawns is an endgame from its start, and so a middlegame too.
    ReplayGame endgame;
    endgame.start_fen = "r3k2r/pppq1ppp/8/8/8/8/PPPQ1PPP/R3K2R w KQkq - 0 1";
    phases            = gamePhases(endgame, false);
    CHECK(phases.middlegame == 0);
    CHECK(phases.endgame == 0);
    CHECK(phaseAt(phases, 0) == Phase::Endgame);
}

TEST_CASE("game graph: White's pawns and winning chances from a side's score or WDL") {
    CHECK(graphPawns(cp(30), chess::Color::WHITE) == doctest::Approx(0.30f));
    CHECK(graphPawns(cp(30), chess::Color::BLACK) == doctest::Approx(-0.30f));
    CHECK(graphPawns(mate(3), chess::Color::BLACK) == -MATE_PAWNS);
    CHECK(graphPawns(mate(0), chess::Color::WHITE) == -MATE_PAWNS);  // mated
    CHECK(std::isnan(graphPawns(std::nullopt, chess::Color::WHITE)));

    CHECK(graphWin(cp(0), std::nullopt, chess::Color::WHITE) == doctest::Approx(0.5f));
    CHECK(graphWin(cp(100), std::nullopt, chess::Color::BLACK) == doctest::Approx(1.0f - 0.591f).epsilon(0.01));
    // The WDL, when there is one, rather than the score: 600 wins, 300 draws, 100 losses for Black.
    const std::array<int64_t, 3> wdl{600, 300, 100};
    CHECK(graphWin(cp(0), wdl, chess::Color::BLACK) == doctest::Approx(0.25f));
    CHECK(std::isnan(graphWin(std::nullopt, std::nullopt, chess::Color::WHITE)));
}

TEST_CASE("game graph: the engines' curves, the book, the time strip and the charts there are values for") {
    const auto game = parseReplay(PGN);
    REQUIRE(game);
    REQUIRE(game->moves.size() == 6);

    auto g = buildGameGraph(*game, false, GraphChart::Eval, {}, {});
    CHECK(g.plies == 6);
    CHECK(g.first_move == 1);
    CHECK_FALSE(g.black_first);
    CHECK(g.book == std::vector<bool>{true, true, false, false, false, false});
    CHECK(g.players[0].name == "Alpha");
    CHECK(g.players[1].name == "Bravo");
    CHECK(g.players[0].key == "white");
    // Each engine at its own moves, White's point of view.
    CHECK(g.players[0].x == std::vector<float>{3, 5});
    CHECK(g.players[0].y[0] == doctest::Approx(0.30f));
    CHECK(g.players[1].x == std::vector<float>{4, 6});
    CHECK(g.players[1].y[1] == doctest::Approx(0.50f));
    CHECK(g.players[1].win.size() == 2);
    // The time strip: each side's moves out of the book.
    CHECK(g.time_x[0] == std::vector<float>{3, 5});
    CHECK(g.time_s[1][0] == doctest::Approx(0.3f));

    const auto has = [&](GraphChart c) { return g.available[static_cast<std::size_t>(c)]; };
    CHECK(has(GraphChart::Eval));
    CHECK(has(GraphChart::Win));
    CHECK(has(GraphChart::Time));
    CHECK(has(GraphChart::Depth));
    CHECK(has(GraphChart::Nodes));
    CHECK(has(GraphChart::Speed));
    CHECK_FALSE(has(GraphChart::TbHits));  // all zero
    CHECK(has(GraphChart::Agreement));     // both engines have evaluations
    CHECK(has(GraphChart::Predicted));     // consecutive moves with PVs
    CHECK_FALSE(has(GraphChart::Wdl));

    // Another chart: its values at each engine's moves, the thinner line beside.
    g = buildGameGraph(*game, false, GraphChart::Depth, {}, {});
    CHECK(g.players[0].y == std::vector<float>{12, 13});
    CHECK(g.players[0].extra[0] == doctest::Approx(20.0f));
    CHECK(std::isnan(g.players[0].extra[1]));
    g = buildGameGraph(*game, false, GraphChart::Predicted, {}, {});
    // Bravo's 2...Nc6 PV repeats the two plies Alpha's 2.Nf3 expected after it (b8c6 f1b5).
    REQUIRE(g.players[1].x.size() == 1);
    CHECK(g.players[1].y[0] == doctest::Approx(2.0f));
}

TEST_CASE("game graph: the analyses, the judging one's marks and the agreement bars") {
    const auto game = parseReplay(PGN);
    REQUIRE(game);
    // Scores of the side to move in each of the 7 positions.
    GameReview first, second;
    for (const int v : {20, -25, 30, -28, 35, -45, 60}) first.positions.push_back({cp(v), "", 10});
    for (const int v : {20, -25, 30, -28, 35, -45, 260}) second.positions.push_back({cp(v), "", 10});
    std::vector<MoveReview> judged(6);
    judged[5].judgement = Judgement::Mistake;

    auto g = buildGameGraph(*game, false, GraphChart::Eval,
                            {{"One", "analysis", 0, &first}, {"Two", "analysis", 0, &second}}, judged);
    REQUIRE(g.analyses.size() == 2);
    CHECK(g.analyses[0].key == "analysis:One");
    CHECK(g.analyses[0].x.size() == 7);
    CHECK(g.analyses[0].y[1] == doctest::Approx(0.25f));  // Black to move, -0.25 for Black
    CHECK(g.analyses[1].y[6] == doctest::Approx(2.60f));  // White to move
    REQUIRE(g.marks.size() == 1);
    CHECK(g.marks[0].ply == 6);
    CHECK(g.marks[0].judgement == Judgement::Mistake);

    // Agreement: the two players apart, and the analyses apart where they both have a value.
    g = buildGameGraph(*game, false, GraphChart::Agreement,
                       {{"One", "analysis", 0, &first}, {"Two", "analysis", 0, &second}}, judged);
    CHECK(g.players[0].key == "apart");
    REQUIRE(g.players[0].x.size() == 3);  // 2...Nc6, 3.Bb5 and 3...a6 against the move before
    CHECK(g.players[0].y[0] == doctest::Approx(0.05f));
    CHECK(g.players[1].x.empty());
    REQUIRE(g.bar_x.size() == 7);
    CHECK(g.bar_y[6] == doctest::Approx(2.0f));
    CHECK(g.bar_y[0] == doctest::Approx(0.0f));
}

TEST_CASE("game graph: a game from a position with Black to move numbers its moves from there") {
    ReplayGame game;
    game.start_fen = "4k3/8/8/8/8/8/4P3/4K3 b - - 0 30";
    auto g         = buildGameGraph(game, false, GraphChart::Eval, {}, {});
    CHECK(g.first_move == 30);
    CHECK(g.black_first);
    CHECK(g.plies == 0);
}

TEST_CASE("game graph: the move under the mouse, told") {
    const auto game = parseReplay(PGN);
    REQUIRE(game);
    CHECK(graphMoveTip(*game, 0).empty());
    CHECK(graphMoveTip(*game, 1).rfind("1.e4", 0) == 0);
    const auto tip = graphMoveTip(*game, 4, "?");
    CHECK(tip.rfind("2…Nc6?", 0) == 0);
    CHECK(tip.find("Bravo") != std::string::npos);
    CHECK(tip.find("12") == std::string::npos);  // Bravo's depth is 11
}
