#pragma once

// The OpenGL 3.3 core entry points the 3D board calls, resolved through SDL like gui/src/gl.hpp: the binary never links
// against libGL. Separate from gl.hpp, whose handful of functions the 2D GUI needs even where OpenGL 3.3 is missing.

#include <cstddef>
#include <cstdint>

namespace fcg::board3d::gl {

using GLenum     = unsigned int;
using GLuint     = unsigned int;
using GLint      = int;
using GLsizei    = int;
using GLfloat    = float;
using GLboolean  = unsigned char;
using GLbitfield = unsigned int;
using GLchar     = char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr   = std::ptrdiff_t;
using GLuint64   = std::uint64_t;

constexpr GLenum TEXTURE_2D          = 0x0DE1;
constexpr GLenum TEXTURE_BUFFER      = 0x8C2A;
constexpr GLenum TEXTURE_MIN_FILTER  = 0x2801;
constexpr GLenum TEXTURE_MAG_FILTER  = 0x2800;
constexpr GLenum TEXTURE_WRAP_S      = 0x2802;
constexpr GLenum TEXTURE_WRAP_T      = 0x2803;
constexpr GLenum NEAREST             = 0x2600;
constexpr GLenum LINEAR              = 0x2601;
constexpr GLenum CLAMP_TO_EDGE       = 0x812F;
constexpr GLenum RGBA                = 0x1908;
constexpr GLenum RGBA8               = 0x8058;
constexpr GLenum RGBA16F             = 0x881A;
constexpr GLenum RGBA32F             = 0x8814;
constexpr GLenum FLOAT               = 0x1406;
constexpr GLenum UNSIGNED_BYTE       = 0x1401;
constexpr GLenum ARRAY_BUFFER        = 0x8892;
constexpr GLenum ELEMENT_ARRAY_BUFFER   = 0x8893;
constexpr GLenum UNSIGNED_INT           = 0x1405;
constexpr GLenum RENDERBUFFER           = 0x8D41;
constexpr GLenum DEPTH_COMPONENT24      = 0x81A6;
constexpr GLenum DEPTH_ATTACHMENT       = 0x8D00;
constexpr GLenum DEPTH_BUFFER_BIT       = 0x00000100;
constexpr GLenum LESS                   = 0x0201;
constexpr GLenum SRC_ALPHA              = 0x0302;
constexpr GLenum ONE_MINUS_SRC_ALPHA    = 0x0303;
constexpr GLenum STATIC_DRAW         = 0x88E4;
constexpr GLenum DYNAMIC_DRAW        = 0x88E8;
constexpr GLenum FRAGMENT_SHADER     = 0x8B30;
constexpr GLenum VERTEX_SHADER       = 0x8B31;
constexpr GLenum COMPILE_STATUS      = 0x8B81;
constexpr GLenum LINK_STATUS         = 0x8B82;
constexpr GLenum INFO_LOG_LENGTH     = 0x8B84;
constexpr GLenum FRAMEBUFFER         = 0x8D40;
constexpr GLenum READ_FRAMEBUFFER    = 0x8CA8;
constexpr GLenum DRAW_FRAMEBUFFER    = 0x8CA9;
constexpr GLenum FRAMEBUFFER_BINDING = 0x8CA6;
constexpr GLenum COLOR_ATTACHMENT0   = 0x8CE0;
constexpr GLenum FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum TEXTURE0            = 0x84C0;
constexpr GLenum TRIANGLES           = 0x0004;
constexpr GLenum BLEND               = 0x0BE2;
constexpr GLenum SCISSOR_TEST        = 0x0C11;
constexpr GLenum DEPTH_TEST          = 0x0B71;
constexpr GLenum CULL_FACE           = 0x0B44;
constexpr GLenum ONE                 = 1;
constexpr GLenum ZERO                = 0;
constexpr GLenum FUNC_ADD            = 0x8006;
constexpr GLenum COLOR_BUFFER_BIT    = 0x00004000;
constexpr GLenum VIEWPORT            = 0x0BA2;
constexpr GLenum CURRENT_PROGRAM     = 0x8B8D;
constexpr GLenum TIME_ELAPSED        = 0x88BF;
constexpr GLenum QUERY_RESULT        = 0x8866;
constexpr GLenum QUERY_RESULT_AVAILABLE = 0x8867;
constexpr GLenum PACK_ALIGNMENT      = 0x0D05;
constexpr GLenum MAJOR_VERSION       = 0x821B;
constexpr GLenum MINOR_VERSION       = 0x821C;

struct Functions {
#define FCG_GL3_FUNCTIONS(X)                                                                        \
    X(void, GenTextures, GLsizei, GLuint*)                                                          \
    X(void, DeleteTextures, GLsizei, const GLuint*)                                                 \
    X(void, BindTexture, GLenum, GLuint)                                                            \
    X(void, ActiveTexture, GLenum)                                                                  \
    X(void, TexParameteri, GLenum, GLenum, GLint)                                                   \
    X(void, TexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) \
    X(void, TexBuffer, GLenum, GLenum, GLuint)                                                      \
    X(void, GenBuffers, GLsizei, GLuint*)                                                           \
    X(void, DeleteBuffers, GLsizei, const GLuint*)                                                  \
    X(void, BindBuffer, GLenum, GLuint)                                                             \
    X(void, BufferData, GLenum, GLsizeiptr, const void*, GLenum)                                    \
    X(void, BufferSubData, GLenum, GLintptr, GLsizeiptr, const void*)                               \
    X(GLuint, CreateShader, GLenum)                                                                 \
    X(void, DeleteShader, GLuint)                                                                   \
    X(void, ShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*)                      \
    X(void, CompileShader, GLuint)                                                                  \
    X(void, GetShaderiv, GLuint, GLenum, GLint*)                                                    \
    X(void, GetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                                   \
    X(GLuint, CreateProgram)                                                                        \
    X(void, DeleteProgram, GLuint)                                                                  \
    X(void, AttachShader, GLuint, GLuint)                                                           \
    X(void, LinkProgram, GLuint)                                                                    \
    X(void, GetProgramiv, GLuint, GLenum, GLint*)                                                   \
    X(void, GetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                                  \
    X(void, UseProgram, GLuint)                                                                     \
    X(GLint, GetUniformLocation, GLuint, const GLchar*)                                             \
    X(void, Uniform1i, GLint, GLint)                                                                \
    X(void, Uniform1f, GLint, GLfloat)                                                              \
    X(void, Uniform2f, GLint, GLfloat, GLfloat)                                                     \
    X(void, Uniform3f, GLint, GLfloat, GLfloat, GLfloat)                                            \
    X(void, Uniform4f, GLint, GLfloat, GLfloat, GLfloat, GLfloat)                                   \
    X(void, Uniform1iv, GLint, GLsizei, const GLint*)                                               \
    X(void, Uniform1fv, GLint, GLsizei, const GLfloat*)                                             \
    X(void, Uniform3fv, GLint, GLsizei, const GLfloat*)                                             \
    X(void, Uniform4fv, GLint, GLsizei, const GLfloat*)                                             \
    X(void, GenVertexArrays, GLsizei, GLuint*)                                                      \
    X(void, DeleteVertexArrays, GLsizei, const GLuint*)                                             \
    X(void, BindVertexArray, GLuint)                                                                \
    X(void, DrawArrays, GLenum, GLint, GLsizei)                                                     \
    X(void, DrawElements, GLenum, GLsizei, GLenum, const void*)                                     \
    X(void, VertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)            \
    X(void, EnableVertexAttribArray, GLuint)                                                        \
    X(void, UniformMatrix4fv, GLint, GLsizei, GLboolean, const GLfloat*)                            \
    X(void, GenRenderbuffers, GLsizei, GLuint*)                                                     \
    X(void, DeleteRenderbuffers, GLsizei, const GLuint*)                                            \
    X(void, BindRenderbuffer, GLenum, GLuint)                                                       \
    X(void, RenderbufferStorage, GLenum, GLenum, GLsizei, GLsizei)                                  \
    X(void, FramebufferRenderbuffer, GLenum, GLenum, GLenum, GLuint)                                \
    X(void, DepthFunc, GLenum)                                                                      \
    X(void, DepthMask, GLboolean)                                                                   \
    X(void, GenFramebuffers, GLsizei, GLuint*)                                                      \
    X(void, DeleteFramebuffers, GLsizei, const GLuint*)                                             \
    X(void, BindFramebuffer, GLenum, GLuint)                                                        \
    X(void, FramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint)                            \
    X(GLenum, CheckFramebufferStatus, GLenum)                                                       \
    X(void, DrawBuffers, GLsizei, const GLenum*)                                                    \
    X(void, Enable, GLenum)                                                                         \
    X(void, Disable, GLenum)                                                                        \
    X(GLboolean, IsEnabled, GLenum)                                                                 \
    X(void, BlendFunc, GLenum, GLenum)                                                              \
    X(void, BlendFuncSeparate, GLenum, GLenum, GLenum, GLenum)                                      \
    X(void, BlendEquation, GLenum)                                                                  \
    X(void, Scissor, GLint, GLint, GLsizei, GLsizei)                                                \
    X(void, Viewport, GLint, GLint, GLsizei, GLsizei)                                               \
    X(void, ClearColor, GLfloat, GLfloat, GLfloat, GLfloat)                                         \
    X(void, Clear, GLbitfield)                                                                      \
    X(void, GetIntegerv, GLenum, GLint*)                                                            \
    X(void, ReadPixels, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)                      \
    X(void, PixelStorei, GLenum, GLint)                                                             \
    X(void, GenQueries, GLsizei, GLuint*)                                                           \
    X(void, DeleteQueries, GLsizei, const GLuint*)                                                  \
    X(void, BeginQuery, GLenum, GLuint)                                                             \
    X(void, EndQuery, GLenum)                                                                       \
    X(void, GetQueryObjectiv, GLuint, GLenum, GLint*)                                               \
    X(void, GetQueryObjectui64v, GLuint, GLenum, GLuint64*)                                         \
    X(void, Flush)                                                                                  \
    X(void, Finish)

#define FCG_GL3_MEMBER(ret, name, ...) ret (*name)(__VA_ARGS__) = nullptr;
    FCG_GL3_FUNCTIONS(FCG_GL3_MEMBER)
#undef FCG_GL3_MEMBER
};

// Resolves every function, with a current context of version 3.3 or later. False, and nothing usable, otherwise.
bool load();

const Functions& fn();

}  // namespace fcg::board3d::gl
