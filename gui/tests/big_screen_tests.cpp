// The big screen: which game it shows next, and the size of its text.

#include <doctest/doctest.hpp>

#include <vector>

#include "big_screen.hpp"

using namespace fcg;

namespace {

BigCandidate game(bool active, std::optional<int> cp = std::nullopt, std::size_t plies = 0, bool mate = false) {
    BigCandidate g;
    g.active  = active;
    g.eval_cp = cp;
    g.plies   = plies;
    g.mate    = mate;
    return g;
}

}  // namespace

TEST_CASE("big screen: in turn, the next game being played, around the slots") {
    const std::vector<BigCandidate> games = {game(true), game(false), game(true), game(true)};
    CHECK(nextGame(games, 0) == 2);
    CHECK(nextGame(games, 2) == 3);
    CHECK(nextGame(games, 3) == 0);
    CHECK(nextGame(games, 1) == 2);  // from a slot between games

    // Alone, or none: the game shown stays.
    CHECK(nextGame({game(false), game(true)}, 1) == 1);
    CHECK(nextGame({game(false), game(false)}, 1) == 1);
    CHECK(nextGame({}, 0) == 0);
}

TEST_CASE("big screen: the most interesting game is the closest, then the longest, and does not jump") {
    // The closest evaluation wins; a mate is decided, a game without an evaluation is level.
    CHECK(mostInteresting({game(true, 250, 80), game(true, -40, 30), game(true, 900, 120)}, 0) == 1);
    CHECK(mostInteresting({game(true, 10, 80, true), game(true, 400, 30)}, 0) == 1);
    CHECK(mostInteresting({game(true, 200, 80), game(true, std::nullopt, 4)}, 0) == 1);

    // Within 0.30 pawns of the closest, the longest game.
    CHECK(mostInteresting({game(true, 5, 40), game(true, -30, 90), game(true, 50, 200)}, 2) == 1);

    // The game shown stays while it is as close as the best.
    CHECK(mostInteresting({game(true, 5, 40), game(true, -30, 90)}, 0) == 0);
    // ...and gives way when it is not, or is over.
    CHECK(mostInteresting({game(true, 300, 40), game(true, -30, 90)}, 0) == 1);
    CHECK(mostInteresting({game(false, 0, 40), game(true, 300, 90)}, 0) == 1);

    // Nothing played: the game shown stays.
    CHECK(mostInteresting({game(false), game(false)}, 1) == 1);
}

TEST_CASE("big screen: its text grows with the window and its own scale, within bounds") {
    CHECK(bigTextSize(1440, 900, 1.0f) == doctest::Approx(30.0f));
    CHECK(bigTextSize(1920, 1080, 1.0f) == doctest::Approx(40.0f));
    CHECK(bigTextSize(3840, 2160, 1.0f) == doctest::Approx(80.0f));
    CHECK(bigTextSize(1920, 1080, 1.5f) == doctest::Approx(60.0f));
    CHECK(bigTextSize(1080, 1920, 1.0f) == doctest::Approx(40.0f));          // a screen turned upright: the same
    CHECK(bigTextSize(1920, 400, 1.0f) == doctest::Approx(400.0f / 27.0f));  // a wide strip: by its height
    CHECK(bigTextSize(200, 100, 1.0f) == doctest::Approx(12.0f));            // never unreadable
    CHECK(bigTextSize(1920, 1080, 9.0f) == doctest::Approx(80.0f));          // the scale is bounded
}
