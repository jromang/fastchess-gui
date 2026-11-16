#pragma once

// The analysis of a finished game, move by move: every position searched by the analysis engine for a fixed time or
// depth, each move judged by the winning chances it lost (an inaccuracy, a mistake, a blunder), and the result kept
// with the game in the profile.

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <types/score.hpp>

#include "analysis.hpp"
#include "pgn_store.hpp"

namespace fcg {

// The chances of the side whose score it is, from 0 to 100, as lichess counts them: 50 + 50 × (2 / (1 + e^(−0.00368208
// × cp)) − 1); a mate for it 100, against it 0.
[[nodiscard]] double winChance(const fastchess::Score& score);

enum class Judgement : uint8_t { None, Inaccuracy, Mistake, Blunder };

// What a move is, from its mover's chances before it and after it (both its own, 0 to 100): a loss of 10 is an
// inaccuracy, of 20 a mistake, of 30 a blunder (lichess' thresholds).
[[nodiscard]] Judgement judge(double before, double after);

// "?!", "?", "??".
[[nodiscard]] const char* judgementMark(Judgement judgement);

// What the engine found in one position of the game.
struct PositionEval {
    std::optional<fastchess::Score> score;  // the side to move's point of view
    std::string best;                       // its best move, UCI; empty at the end of the game
    int depth       = 0;
    int64_t time_ms = 0;                        // how long it was searched
    std::string pv;                             // its principal variation, UCI moves from the position
    std::optional<std::array<int64_t, 3>> wdl;  // win, draw, loss in permille, the side to move's (UCI_ShowWDL)
};

// A whole game's analysis by one engine: one PositionEval per position, from the start (plies + 1 of them).
struct GameReview {
    std::string engine;       // what it was made with
    int64_t movetime_ms = 0;  // the time per position (of its last complete pass)
    int depth           = 0;  // the depth per position, for an analysis made at a fixed depth
    int passes          = 0;  // the continuous analysis's passes completed; 0 for an analysis made in one go
    std::vector<PositionEval> positions;
    // Each move's judgement at the pass before the last one (Judgement as an int), to show what the last pass
    // changed; empty after one pass.
    std::vector<int8_t> previous;

    [[nodiscard]] bool complete(std::size_t plies) const { return positions.size() == plies + 1; }
};

// The continuous analysis: the time per position of a pass, 4 times longer at each from FIRST_PASS_MS.
inline constexpr int64_t FIRST_PASS_MS = 20;
[[nodiscard]] constexpr int64_t passTime(int pass) noexcept { return FIRST_PASS_MS << (2 * std::min(pass, 12)); }

// A move of the game, judged.
struct MoveReview {
    Judgement judgement = Judgement::None;
    double loss         = 0;      // the mover's winning chances lost, 0 to 100
    bool best           = false;  // the engine's own choice
};

// The moves of `game` judged by `review` (as many as it has positions for). A move the engine would have played is
// not judged.
[[nodiscard]] std::vector<MoveReview> reviewMoves(const ReplayGame& game, bool chess960, const GameReview& review);

// How many of each judgement each side got: [colour][Inaccuracy, Mistake, Blunder], White first.
[[nodiscard]] std::array<std::array<int, 3>, 2> judgementCounts(const ReplayGame& game,
                                                                const std::vector<MoveReview>& moves);

// The score of a position the game ended in, without an engine: mated (mate 0 against the side to move) or drawn
// (stalemate, material, repetition, 50 moves). nullopt when the game goes on there.
[[nodiscard]] std::optional<fastchess::Score> endScore(const chess::Board& board);

// The key a game's analysis is kept under: its start position and moves.
[[nodiscard]] std::string reviewKey(const ReplayGame& game);

// The analyses of a game, kept in `dir` (analyses/ in the profile), one file per game, the judging one first; none
// when there is no file or it is not the game's.
[[nodiscard]] std::vector<GameReview> loadReviews(const std::filesystem::path& dir, const ReplayGame& game);
bool saveReviews(const std::filesystem::path& dir, const ReplayGame& game, const std::vector<GameReview>& reviews);
// The judging one alone.
[[nodiscard]] std::optional<GameReview> loadReview(const std::filesystem::path& dir, const ReplayGame& game);
bool saveReview(const std::filesystem::path& dir, const ReplayGame& game, const GameReview& review);

}  // namespace fcg
