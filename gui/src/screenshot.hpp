#pragma once

#include <string>

namespace fcg {

// Saves the current back buffer as PNG. Call after rendering, before swapping.
bool saveScreenshot(const std::string& path, int width, int height);

}  // namespace fcg
