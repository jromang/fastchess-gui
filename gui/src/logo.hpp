#pragma once

// The logo, a pawn whose head is a stopwatch, in one colour on a rounded tile: drawn in the navigation rail, and
// rasterized for the window icon, the Windows executable's icon (gui/win/fastchess-gui.ico) and the macOS
// application's (gui/mac/fastchess-gui.icns).

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

// The application icon as an .icns file, for the macOS application bundle: 16 to 1024 pixels, PNG images, the tile
// with the margin macOS gives its icons.
[[nodiscard]] std::vector<uint8_t> icnsFile();

}  // namespace fcg::logo
