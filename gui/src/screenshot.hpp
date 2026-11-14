#pragma once

#include <string>

namespace fcg {

// Saves `width` x `height` RGBA pixels as a PNG file: rows `stride` bytes apart, from the top unless `bottom_up` (as
// OpenGL reads them). render::screenshot() reads the window's pixels.
bool savePng(const std::string& path, int width, int height, const void* rgba, int stride, bool bottom_up);

}  // namespace fcg
