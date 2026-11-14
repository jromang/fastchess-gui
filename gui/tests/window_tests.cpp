#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>

#include <fmt/include/fmt/format.h>

#include "storage.hpp"
#include "window_place.hpp"

using namespace fcg;
namespace fs = std::filesystem;

namespace {

const ScreenRect LEFT{0, 0, 1920, 1040};      // primary, a task bar at the bottom
const ScreenRect RIGHT{1920, 0, 2560, 1400};  // a second screen on its right

WindowPlace place(int x, int y, int w, int h, bool maximized = false) {
    WindowPlace p;
    p.x         = x;
    p.y         = y;
    p.w         = w;
    p.h         = h;
    p.maximized = maximized;
    return p;
}

}  // namespace

TEST_CASE("window: the default size, centred, maximized when it does not fit") {
    const auto o = openingPlace(std::nullopt, {LEFT, RIGHT}, 1.0f);
    CHECK(o.w == 1440);
    CHECK(o.h == 900);
    CHECK(o.x == (1920 - 1440) / 2);
    CHECK(o.y == 70);
    CHECK_FALSE(o.maximize);

    const auto small = openingPlace(std::nullopt, {{0, 0, 1366, 728}}, 1.0f);
    CHECK(small.maximize);
    CHECK(small.w <= 1366 - 32);
    CHECK(small.h <= 728 - 96);

    // No screen known: the system centres it.
    const auto none = openingPlace(std::nullopt, {}, 2.0f);
    CHECK(none.w == 2880);
    CHECK_FALSE(none.x.has_value());
}

TEST_CASE("window: it opens where it was left, maximized if it was") {
    auto o = openingPlace(place(100, 50, 1200, 800), {LEFT, RIGHT}, 1.0f);
    CHECK(o.x == 100);
    CHECK(o.y == 50);
    CHECK(o.w == 1200);
    CHECK(o.h == 800);
    CHECK_FALSE(o.maximize);

    // On the second screen, larger than the first one would take.
    o = openingPlace(place(2000, 30, 2400, 1300, true), {LEFT, RIGHT}, 1.0f);
    CHECK(o.x == 2000);
    CHECK(o.w == 2400);
    CHECK(o.maximize);
}

TEST_CASE("window: brought back on a screen when the one it was on is gone") {
    // It was on the second screen, now unplugged: centred on the first one, at its size.
    auto o = openingPlace(place(2000, 30, 1200, 800), {LEFT}, 1.0f);
    CHECK(o.w == 1200);
    CHECK(o.x == (1920 - 1200) / 2);
    CHECK(o.y >= 0);
    CHECK(*o.y + o.h <= 1040);

    // Too large for the screen left: the default size.
    o = openingPlace(place(2000, 30, 2400, 1300), {LEFT}, 1.0f);
    CHECK(o.w == 1440);
    CHECK(*o.x + o.w <= 1920);

    // Its title bar above the top edge, or only a few pixels of it left on the screen: moved too.
    o = openingPlace(place(100, -500, 1200, 800), {LEFT, RIGHT}, 1.0f);
    CHECK(o.y >= 0);
    o = openingPlace(place(1900, 100, 1200, 800), {LEFT}, 1.0f);
    CHECK(*o.x + o.w <= 1920);
    // Half of it on the screen is fine: it can be dragged back.
    o = openingPlace(place(1300, 100, 1200, 800), {LEFT}, 1.0f);
    CHECK(o.x == 1300);
}

TEST_CASE("window: a size kept that no longer fits the screen it is on") {
    // The resolution was lowered.
    const auto o = openingPlace(place(0, 0, 1900, 1030), {{0, 0, 1280, 680}}, 1.0f);
    CHECK(o.w <= 1280);
    CHECK(o.h <= 680);
    CHECK(o.maximize);
}

TEST_CASE("window: without its place (Wayland), centred at the size it had") {
    WindowPlace p;
    p.w          = 1000;
    p.h          = 700;
    p.maximized  = true;
    const auto o = openingPlace(p, {LEFT}, 1.0f);
    CHECK(o.w == 1000);
    CHECK(o.h == 700);
    CHECK(o.x == 460);
    CHECK(o.maximize);
    // A size too small to be meant: the default.
    p.w = 10;
    CHECK(openingPlace(p, {LEFT}, 1.0f).w == 1440);
}

TEST_CASE("window: its place is kept in settings.json") {
    const auto dir = fs::temp_directory_path() /
                     fmt::format("fcg-tests-window-{}", std::chrono::steady_clock::now().time_since_epoch().count());
    const Storage storage(dir);
    CHECK_FALSE(storage.windowPlace().has_value());

    storage.setLichessToken("secret");  // another setting of the file stays
    storage.setWindowPlace(place(-1500, 20, 1300, 820, true));
    auto read = storage.windowPlace();
    REQUIRE(read.has_value());
    CHECK(read->x == -1500);
    CHECK(read->y == 20);
    CHECK(read->w == 1300);
    CHECK(read->h == 820);
    CHECK(read->maximized);
    CHECK(storage.lichessToken() == "secret");

    WindowPlace wayland;
    wayland.w = 900;
    wayland.h = 600;
    storage.setWindowPlace(wayland);
    read = storage.windowPlace();
    REQUIRE(read.has_value());
    CHECK_FALSE(read->x.has_value());
    CHECK(read->w == 900);

    std::error_code ec;
    fs::remove_all(dir, ec);
}
