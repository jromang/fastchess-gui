#include "window_place.hpp"

#include <algorithm>

namespace fcg {

namespace {

// The strip along the top of the window, where its title bar is held: enough of it on a screen to be dragged.
constexpr int TOP_STRIP       = 32;
constexpr int STRIP_VISIBLE   = 64;  // wide, at least (the whole strip for a narrower window)
constexpr int STRIP_VISIBLE_Y = 16;

int overlap(int a, int a_size, int b, int b_size) {
    return std::max(0, std::min(a + a_size, b + b_size) - std::max(a, b));
}

// The screen whose usable area has most of the window's top strip, enough of it to be grabbed; none when it is
// off every screen (a screen removed, a resolution lowered).
std::optional<ScreenRect> screenOf(int x, int y, int w, const std::vector<ScreenRect>& screens) {
    std::optional<ScreenRect> best;
    int best_area = 0;
    for (const auto& s : screens) {
        const int ow = overlap(x, w, s.x, s.w), oh = overlap(y, TOP_STRIP, s.y, s.h);
        if (ow < std::min(w, STRIP_VISIBLE) || oh < STRIP_VISIBLE_Y) continue;
        if (ow * oh > best_area) {
            best_area = ow * oh;
            best      = s;
        }
    }
    return best;
}

// Centred in `screen`, below the title bar.
void centre(WindowOpening& o, const ScreenRect& screen, float scale) {
    o.x = screen.x + (screen.w - o.w) / 2;
    o.y = screen.y + std::max(static_cast<int>(48 * scale), (screen.h - o.h) / 2);
}

}  // namespace

void to_json(nlohmann::json& j, const WindowPlace& place) {
    j = nlohmann::json{{"w", place.w}, {"h", place.h}, {"maximized", place.maximized}};
    if (place.x && place.y) {
        j["x"] = *place.x;
        j["y"] = *place.y;
    }
}

void from_json(const nlohmann::json& j, WindowPlace& place) {
    place   = WindowPlace{};
    place.w = j.value("w", 0);
    place.h = j.value("h", 0);
    if (j.contains("x") && j.contains("y")) {
        place.x = j.at("x").get<int>();
        place.y = j.at("y").get<int>();
    }
    place.maximized = j.value("maximized", false);
}

WindowOpening defaultOpening(const std::optional<ScreenRect>& screen, float scale) {
    WindowOpening o;
    o.w = static_cast<int>(1440 * scale);
    o.h = static_cast<int>(900 * scale);
    if (!screen || screen->w <= 0 || screen->h <= 0) return o;
    const int margin_x = static_cast<int>(32 * scale);
    const int margin_y = static_cast<int>(96 * scale);  // title bar above, some room below
    if (o.w > screen->w - margin_x || o.h > screen->h - margin_y) {
        o.w        = std::max(640, std::min(o.w, screen->w - margin_x));
        o.h        = std::max(480, std::min(o.h, screen->h - margin_y));
        o.maximize = true;
    }
    centre(o, *screen, scale);
    return o;
}

WindowOpening openingPlace(const std::optional<WindowPlace>& saved, const std::vector<ScreenRect>& screens,
                           float scale) {
    const std::optional<ScreenRect> primary = screens.empty() ? std::nullopt : std::optional(screens.front());
    if (!saved || saved->w < 200 || saved->h < 150) return defaultOpening(primary, scale);

    WindowOpening o;
    o.w        = saved->w;
    o.h        = saved->h;
    o.maximize = saved->maximized;
    if (!primary) return o;  // no screen known: the system places it

    // Where it was, if that is still on a screen and it fits there.
    if (saved->x && saved->y) {
        if (const auto screen = screenOf(*saved->x, *saved->y, saved->w, screens)) {
            if (saved->w <= screen->w && saved->h <= screen->h) {
                o.x = saved->x;
                o.y = saved->y;
                return o;
            }
            auto fitted     = defaultOpening(screen, scale);
            fitted.maximize = fitted.maximize || saved->maximized;
            return fitted;
        }
    }
    // Its place unknown or off every screen: centred on the primary one, with room for the title bar, as the default.
    const int margin_x = static_cast<int>(32 * scale), margin_y = static_cast<int>(96 * scale);
    if (saved->w > primary->w - margin_x || saved->h > primary->h - margin_y) {
        auto fitted     = defaultOpening(primary, scale);
        fitted.maximize = fitted.maximize || saved->maximized;
        return fitted;
    }
    centre(o, *primary, scale);
    return o;
}

}  // namespace fcg
