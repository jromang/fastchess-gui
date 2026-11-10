#pragma once

// The logo, a pawn whose head is a stopwatch, in one colour on a rounded tile: drawn in the navigation rail, and
// rasterized for the window icon and the Windows executable's icon (gui/win/fastchess-gui.ico).

#include <cstdint>
#include <vector>

#include <imgui.h>

namespace fcg::logo {

// The tile at `min`, `size` wide, and the pawn on it.
void draw(ImDrawList* dl, ImVec2 min, float size, ImU32 tile, ImU32 glyph);

// The application icon, `size` x `size` RGBA pixels: the light theme's blue tile, the pawn in white.
[[nodiscard]] std::vector<uint8_t> icon(int size);

// The application icon as a .ico file: 16 to 256 pixels, PNG images.
[[nodiscard]] std::vector<uint8_t> icoFile();

}  // namespace fcg::logo
