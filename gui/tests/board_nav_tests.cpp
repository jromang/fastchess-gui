// Through the moves of the game a board shows: the steps of the buttons and keys, and a live board that stays on an
// earlier position while the game goes on, then follows it again.

#include <doctest/doctest.hpp>

#include "board_nav.hpp"

using namespace fcg;

TEST_CASE("board navigation: each step stays within the game") {
    CHECK(stepPly(NavStep::First, 7, 20) == 0);
    CHECK(stepPly(NavStep::Previous, 7, 20) == 6);
    CHECK(stepPly(NavStep::Next, 7, 20) == 8);
    CHECK(stepPly(NavStep::Last, 7, 20) == 20);
    CHECK(stepPly(NavStep::None, 7, 20) == 7);
    // At either end, nowhere further to go.
    CHECK(stepPly(NavStep::Previous, 0, 20) == 0);
    CHECK(stepPly(NavStep::Next, 20, 20) == 20);
    // A game without moves: the start position only.
    CHECK(stepPly(NavStep::Next, 0, 0) == 0);
    CHECK(stepPly(NavStep::Last, 0, 0) == 0);
    // A ply out of the game (a shorter game now shown) is brought back into it.
    CHECK(stepPly(NavStep::None, 30, 20) == 20);
    CHECK(stepPly(NavStep::Previous, 30, 20) == 20);
    CHECK(stepPly(NavStep::None, -3, 20) == 0);
}

TEST_CASE("board navigation: a live board follows the game until the user steps back") {
    FollowedPly nav;
    CHECK(nav.shown(10) == 10);
    CHECK_FALSE(nav.held(10));
    // The game goes on: the board with it.
    CHECK(nav.shown(11) == 11);

    // A step back: that position stays while the engines play on.
    nav.step(NavStep::Previous, 11);
    CHECK(nav.held(11));
    CHECK(nav.shown(11) == 10);
    CHECK(nav.shown(12) == 10);
    CHECK(nav.shown(15) == 10);
    nav.step(NavStep::First, 15);
    CHECK(nav.shown(16) == 0);

    // Stepping forward up to the last move: the board follows the game again.
    nav.show(14, 16);
    nav.step(NavStep::Next, 16);
    CHECK(nav.shown(16) == 15);
    nav.step(NavStep::Next, 16);
    CHECK_FALSE(nav.held(16));
    CHECK(nav.shown(17) == 17);
}

TEST_CASE("board navigation: Last, a click on the last move or a new game follow the game again") {
    FollowedPly nav;
    nav.show(4, 20);
    CHECK(nav.held(20));
    nav.step(NavStep::Last, 20);
    CHECK_FALSE(nav.held(21));
    CHECK(nav.shown(21) == 21);

    // A move clicked in the list: the last one is the game itself.
    nav.show(12, 21);
    CHECK(nav.shown(25) == 12);
    nav.show(25, 25);
    CHECK(nav.shown(26) == 26);

    // A position held, then the next game in the slot.
    nav.show(3, 40);
    nav.follow();
    CHECK(nav.shown(0) == 0);
    CHECK(nav.shown(2) == 2);
    CHECK_FALSE(nav.held(2));
}

TEST_CASE("board navigation: the start of a game, and a held position the game has not reached") {
    FollowedPly nav;
    // Before the first move: nothing to step back to.
    nav.step(NavStep::Previous, 0);
    CHECK_FALSE(nav.held(0));
    CHECK(nav.shown(0) == 0);
    CHECK(nav.shown(1) == 1);

    // The start position held: shown however long the game gets.
    nav.step(NavStep::First, 5);
    CHECK(nav.held(5));
    CHECK(nav.shown(80) == 0);

    // A position beyond the moves of the game shown (a shorter game): its last position.
    nav.show(8, 10);
    CHECK(nav.shown(6) == 6);
    CHECK_FALSE(nav.held(6));
    nav.step(NavStep::None, 6);
    CHECK(nav.shown(6) == 6);
}
