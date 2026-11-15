#pragma once

// Openings that decide the game by themselves: both engines, as soon as they are out of the book, see the same side
// far ahead. Told from the evaluations of a finished game, when it ends, so that a tester sees them as the games come
// (and the tournament may play the pair again from another opening) rather than in the results by opening afterwards.

#include <cstdint>
#include <optional>
#include <vector>

#include <types/match_data.hpp>
#include <types/score.hpp>

namespace fcg::tour {

// Both engines beyond `score_cp` for the same side, for each of their first `moves` moves after the book.
struct UnbalancedRule {
    int score_cp = 200;
    int moves    = 4;
};

// One move of a game as the rule reads it: who played it, a book move or not, and the engine's score from its own
// point of view (none for a book move, or an engine that gave none).
struct PlyEval {
    bool white = true;
    bool book  = false;
    std::optional<fastchess::Score> score;
};

// +1 when the opening was unbalanced for White, -1 for Black, 0 otherwise: every one of the first `moves` moves each
// engine played after the book (its own book's moves are book moves too) has a score beyond `score_cp` for that side,
// from White's point of view, a mate counting as beyond any. A move without a score among them, or a game in which an
// engine played fewer moves than that after the book, tells nothing: 0. A rule of 0 cp or 0 moves is off: 0.
[[nodiscard]] int unbalancedOpening(const std::vector<PlyEval>& plies, const UnbalancedRule& rule);

// The same from a game fastchess played (its moves after `data.fen`, the book moves among them marked).
[[nodiscard]] int unbalancedOpening(const fastchess::MatchData& data, const UnbalancedRule& rule);

// The reason given to the games of a pair voided and played again from another opening, because it was unbalanced.
inline constexpr const char* UNBALANCED_REASON = "unbalanced opening";

// How many times one encounter's opening is replaced at most: the book might be full of such openings, and a pair
// must end some day. The pair played from the last one counts, flagged.
inline constexpr int MAX_OPENING_REPLACEMENTS = 3;

}  // namespace fcg::tour
