#include "board3d/gl3.hpp"

#include <SDL3/SDL_video.h>

namespace fcg::board3d::gl {

namespace {
Functions g_functions;
bool g_loaded = false;

template <typename F>
bool resolve(F& target, const char* name) {
    target = reinterpret_cast<F>(SDL_GL_GetProcAddress(name));
    return target != nullptr;
}
}  // namespace

bool load() {
    if (g_loaded) return true;
    bool ok = true;
#define FCG_GL3_RESOLVE(ret, name, ...) ok &= resolve(g_functions.name, "gl" #name);
    FCG_GL3_FUNCTIONS(FCG_GL3_RESOLVE)
#undef FCG_GL3_RESOLVE
    if (!ok) return false;
    GLint major = 0, minor = 0;
    g_functions.GetIntegerv(MAJOR_VERSION, &major);
    g_functions.GetIntegerv(MINOR_VERSION, &minor);
    g_loaded = major > 3 || (major == 3 && minor >= 3);
    return g_loaded;
}

const Functions& fn() { return g_functions; }

}  // namespace fcg::board3d::gl
