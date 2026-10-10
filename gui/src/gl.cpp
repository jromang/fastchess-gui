#include "gl.hpp"

#include <SDL3/SDL_video.h>

namespace fcg::gl {

namespace {
Functions g_functions;

template <typename F>
bool resolve(F& target, const char* name) {
    target = reinterpret_cast<F>(SDL_GL_GetProcAddress(name));
    return target != nullptr;
}
}  // namespace

bool load() {
    bool ok = true;
    ok &= resolve(g_functions.GenTextures, "glGenTextures");
    ok &= resolve(g_functions.DeleteTextures, "glDeleteTextures");
    ok &= resolve(g_functions.BindTexture, "glBindTexture");
    ok &= resolve(g_functions.TexParameteri, "glTexParameteri");
    ok &= resolve(g_functions.TexImage2D, "glTexImage2D");
    ok &= resolve(g_functions.GenerateMipmap, "glGenerateMipmap");
    ok &= resolve(g_functions.PixelStorei, "glPixelStorei");
    ok &= resolve(g_functions.Viewport, "glViewport");
    ok &= resolve(g_functions.ClearColor, "glClearColor");
    ok &= resolve(g_functions.Clear, "glClear");
    ok &= resolve(g_functions.ReadPixels, "glReadPixels");
    return ok;
}

const Functions& fn() { return g_functions; }

}  // namespace fcg::gl
