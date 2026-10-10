#include "screenshot.hpp"

#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "gl.hpp"

namespace fcg {

bool saveScreenshot(const std::string& path, int width, int height) {
    if (width <= 0 || height <= 0) return false;

    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
    gl::fn().PixelStorei(gl::PACK_ALIGNMENT, 1);
    gl::fn().ReadPixels(0, 0, width, height, gl::RGBA, gl::UNSIGNED_BYTE, pixels.data());

    // OpenGL rows go bottom to top.
    stbi_flip_vertically_on_write(1);
    return stbi_write_png(path.c_str(), width, height, 4, pixels.data(), width * 4) != 0;
}

}  // namespace fcg
