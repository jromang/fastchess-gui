#pragma once

// The handful of OpenGL entry points the GUI calls itself (the ImGui backend has its own loader).
// They are resolved through SDL at startup, so the binary never links against libGL.

#include <cstddef>
#include <cstdint>

namespace fcg::gl {

using GLenum     = unsigned int;
using GLuint     = unsigned int;
using GLint      = int;
using GLsizei    = int;
using GLfloat    = float;
using GLbitfield = unsigned int;

constexpr GLenum TEXTURE_2D           = 0x0DE1;
constexpr GLenum TEXTURE_MIN_FILTER   = 0x2801;
constexpr GLenum TEXTURE_MAG_FILTER   = 0x2800;
constexpr GLenum TEXTURE_WRAP_S       = 0x2802;
constexpr GLenum TEXTURE_WRAP_T       = 0x2803;
constexpr GLenum LINEAR               = 0x2601;
constexpr GLenum LINEAR_MIPMAP_LINEAR = 0x2703;
constexpr GLenum CLAMP_TO_EDGE        = 0x812F;
constexpr GLenum RGBA                 = 0x1908;
constexpr GLenum UNSIGNED_BYTE        = 0x1401;
constexpr GLenum UNPACK_ALIGNMENT     = 0x0CF5;
constexpr GLenum PACK_ALIGNMENT       = 0x0D05;
constexpr GLbitfield COLOR_BUFFER_BIT = 0x00004000;

struct Functions {
    void (*GenTextures)(GLsizei, GLuint*)                                                          = nullptr;
    void (*DeleteTextures)(GLsizei, const GLuint*)                                                 = nullptr;
    void (*BindTexture)(GLenum, GLuint)                                                            = nullptr;
    void (*TexParameteri)(GLenum, GLenum, GLint)                                                   = nullptr;
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
    void (*GenerateMipmap)(GLenum)                                                                 = nullptr;
    void (*PixelStorei)(GLenum, GLint)                                                             = nullptr;
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei)                                               = nullptr;
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat)                                         = nullptr;
    void (*Clear)(GLbitfield)                                                                      = nullptr;
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)                      = nullptr;
};

// Resolves every function. Needs a current context. Returns false if one is missing.
bool load();

const Functions& fn();

}  // namespace fcg::gl
