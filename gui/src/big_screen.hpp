#pragma once

// The big screen: one game of the live view filling a window, for a television, a club or a stream. What it shows
// next when it follows the games in progress, and the size of its text.

#include <cstddef>
#include <optional>
#include <vector>

namespace fcg {

// What the big screen follows: one game, every game in progress in turn, or the most interesting one.
enum class BigFollow { Game = 0, InTurn = 1, Interesting = 2 };

// A game of the live view (a slot), as the big screen weighs it.
struct BigCandidate {
    bool active = false;         // being played
    std::optional<int> eval_cp;  // the last evaluation, in centipawns from White's point of view; none yet
    bool mate         = false;   // a mate is announced: the game is decided
    std::size_t plies = 0;
};

// The game shown after `current` in turn: the next one being played, in the order of the slots; `current` when no
// other one is.
[[nodiscard]] int nextGame(const std::vector<BigCandidate>& games, int current);

// The most interesting game being played: the closest evaluation, then the longest game. Evaluations within 0.30
// pawns of the closest count as as close, a game without one yet as level, a mate announced as decided. `current`
// stays while it is among the closest, so that the screen does not jump between games as interesting; `current` too
// when no game is being played.
[[nodiscard]] int mostInteresting(const std::vector<BigCandidate>& games, int current);

// The base size of the big screen's text, in pixels, for a window of `width` x `height` pixels at `scale` (the big
// screen's own, 1 by default): the names, the clocks and the moves are multiples of it.
[[nodiscard]] float bigTextSize(float width, float height, float scale);

}  // namespace fcg
