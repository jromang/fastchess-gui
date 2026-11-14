#include "screenshot.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace fcg {

bool savePng(const std::string& path, int width, int height, const void* rgba, int stride, bool bottom_up) {
    if (width <= 0 || height <= 0 || !rgba) return false;
    stbi_flip_vertically_on_write(bottom_up ? 1 : 0);
    return stbi_write_png(path.c_str(), width, height, 4, rgba, stride) != 0;
}

}  // namespace fcg
