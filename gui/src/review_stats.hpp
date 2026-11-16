#pragma once

// What the Review tab of the Board page tells of an analysed game: each side's accuracy (lichess's formula), average
// loss, judgements, agreement with the analysis, depth and time, its accuracy in each phase; the key moments; who saw
// the turning point first; and the game written with its analysis.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game_analysis.hpp"
#include "game_graph.hpp"
#include "pgn_store.hpp"

namespace fcg {

// A move's accuracy from its mover's winning chances before and after it (0 to 100): lichess's
// 103.1668 × e^(−0.04354 × loss) − 3.1669, within 0 and 100.
[[nodiscard]] double moveAccuracy(double before, double after);

struct SideStats {
    int moves = 0;                       // judged: out of the book, both positions analysed
    std::optional<double> accuracy;      // lichess: the mean of the volatility-weighted mean and the harmonic mean
    std::array<int, 3> judgements{};     // inaccuracies, mistakes, blunders
    std::optional<double> average_loss;  // centipawns, each loss within 0 and 1000
    std::optional<double> agreement;     // the share of its moves the analysis would also play, 0 to 1
    std::optional<double> depth;         // its engine's average depth, from the PGN
    int64_t time_ms = 0;                 // its engine's time, from the PGN
    std::array<std::optional<double>, 3> phase_accuracy;  // opening, middlegame, endgame
};

// Each side's figures (White's first) by `review` (plies + 1 positions), `judged` its judgements.
[[nodiscard]] std::array<SideStats, 2> gameStats(const ReplayGame& game, bool chess960, const GameReview& review,
                                                 const std::vector<MoveReview>& judged, const GamePhases& phases);

struct KeyMoment {
    enum class Kind : uint8_t { TurningPoint, Judged, Disagreement, Endgame, Tablebase };
    Kind kind           = Kind::Judged;
    int ply             = 0;  // the position after the move (for the endgame and the tablebase: the position it starts)
    double before       = 0;  // TurningPoint, Judged: White's pawns before the move and after it; Disagreement: the two
    double after        = 0;  // engines' evaluations (White's), the mover's first
    Judgement judgement = Judgement::None;
};

// The moments worth a look, in the game's order: the move that lost the most (when it is a mistake or a blunder),
// the other mistakes and blunders, where the two engines disagreed by 1.5 pawns or more for two moves in a row (at
// most three), the start of the endgame and of the tablebase zone (`tablebase`: -1 none).
[[nodiscard]] std::vector<KeyMoment> keyMoments(const ReplayGame& game, const GameReview& review,
                                                const std::vector<MoveReview>& judged, const GamePhases& phases,
                                                int tablebase);

// When each curve saw the turning point at `ply` coming: the first position from 12 plies before it where the curve
// gives the side it favours a pawn or more (White's pawns, `sign` +1 for White); -1 when it never did.
[[nodiscard]] int sawItAt(const GraphCurve& curve, int ply, int sign);

// The game's PGN with the analysis written in: each judged move's NAG ($6 ?!, $2 ?, $4 ??), the analysis's
// evaluation of each position as [%eval] (White's), and after a judged move the line the analysis preferred.
[[nodiscard]] std::string pgnWithAnalysis(const GameTree& tree, bool chess960, const GameReview& review,
                                          const std::vector<MoveReview>& judged);

}  // namespace fcg
