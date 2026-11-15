#pragma once

// Through the moves of the game a board shows: the steps (first, previous, next, last position), the keys and the row
// of small buttons under the boards that ask for them, and a board that follows a game being played unless the user
// stepped back in it.

#include <optional>

namespace fcg {

// Ply 0 is the start position, `total` the position after the last move.
enum class NavStep { None, First, Previous, Next, Last };

// The ply a step leads to from `ply`, within [0, total].
[[nodiscard]] int stepPly(NavStep step, int ply, int total) noexcept;

// A board that follows a game being played, unless the user stepped back in it: it then stays on the position chosen
// while the game goes on, and follows the game again once brought back to its last move.
class FollowedPly {
   public:
    // The ply shown in a game of `total` plies: the one held, else the last.
    [[nodiscard]] int shown(int total) const noexcept;
    // True when an earlier position than the last is shown, the board not following the game.
    [[nodiscard]] bool held(int total) const noexcept;
    // Shows `ply`; from the last position on, the board follows the game again.
    void show(int ply, int total) noexcept;
    void step(NavStep step, int total) noexcept;
    // Follows the game again (a new game).
    void follow() noexcept { held_.reset(); }

   private:
    std::optional<int> held_;
};

// The step the keys ask for this frame: the left and right arrows, Home and End; F turns `flip` over. Nothing while a
// text field or a popup has the keys, or with Ctrl, Alt or Super held.
NavStep navKeys(bool& flip);

// The height the row of buttons takes under a board.
[[nodiscard]] float boardNavHeight();

struct NavClick {
    NavStep step = NavStep::None;
    bool hide    = false;  // "Hide the navigation buttons" chosen in the menu of a right click on the row
};

// The row of small buttons under a board showing a game, at the cursor: first, previous, next and last position, and
// Flip, the keys in their tooltips. Without `buttons` (the setting hides them), nothing but what follows. With `live`
// (a game being played) and an earlier position shown, "Back to the current move" after them, or only its icon when
// the row, `width` wide, has no room for the words.
NavClick drawBoardNav(const char* id, int ply, int total, bool& flip, bool buttons, bool live, float width);

// Over the bottom of the board just drawn (the last item), "Back to the current move", for a board that no longer
// follows its game while the buttons under it are hidden. Takes no room. True when clicked.
bool drawBackOverBoard();

}  // namespace fcg
