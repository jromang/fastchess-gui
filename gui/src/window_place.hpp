#pragma once

// Where the main window opens: as it was left (its size, its place, maximized or not), brought back on a screen when
// the one it was on is gone, at the default size when the size it had does not fit.

#include <optional>
#include <vector>

#include <json.hpp>

namespace fcg {

// A rectangle in the screen coordinates of SDL (points, not pixels, on a high density screen).
struct ScreenRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const ScreenRect&) const = default;
};

// The main window as it was left: its size and place when it was last neither maximized nor minimized, and whether
// it was maximized. Kept in settings.json.
struct WindowPlace {
    int w = 0, h = 0;
    std::optional<int> x, y;  // none where windows are not told their place (Wayland)
    bool maximized = false;
};
void to_json(nlohmann::json& j, const WindowPlace& place);
void from_json(const nlohmann::json& j, WindowPlace& place);

// How the window opens.
struct WindowOpening {
    int w = 0, h = 0;
    std::optional<int> x, y;  // none: centred by the system (no screen known)
    bool maximize = false;
};

// The default: 1440 x 900 at `scale`, centred in the usable area of `screen` (the screen without its task bar),
// below the title bar; a window larger than the screen would have its title bar above its top edge, so when the
// default does not fit, it is made smaller and opens maximized.
[[nodiscard]] WindowOpening defaultOpening(const std::optional<ScreenRect>& screen, float scale);

// The window as `saved` left it, on the usable areas of the screens (`screens`, the primary one first): at its place
// when its top edge (the title bar) is on one of them, else centred on the primary one; at the default size when
// its size does not fit the screen it opens on. The default without `saved`.
[[nodiscard]] WindowOpening openingPlace(const std::optional<WindowPlace>& saved,
                                         const std::vector<ScreenRect>& screens, float scale);

}  // namespace fcg
