#include "renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>

#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <fmt/include/fmt/format.h>

#include "console.hpp"
#include "gl.hpp"
#include "i18n.hpp"
#include "screenshot.hpp"

// Dear ImGui's OpenGL backend loads the functions it needs itself, when it starts. Called before, so that a system
// where it cannot gets SDL's renderer instead of a window that draws nothing.
bool ImGui_ImplOpenGL3_InitLoader();

namespace fcg::render {

namespace {

struct State {
    Path path                = Path::None;
    SDL_Window* window       = nullptr;
    SDL_GLContext gl_context = nullptr;
    SDL_Renderer* renderer   = nullptr;
    bool backends            = false;
    bool software            = false;  // SDL's software driver
    std::uint64_t last_frame = 0;      // frameHash() of the frame the software driver drew last, and when
    std::chrono::steady_clock::time_point last_draw{};
    std::string gl_renderer;  // GL_RENDERER: the graphics card, or Mesa's llvmpipe
    std::string gl_version;   // GL_VERSION
    std::string fallback;
    std::string failure;
};
State g;

std::string glString(gl::GLenum name) {
    const unsigned char* s = gl::fn().GetString ? gl::fn().GetString(name) : nullptr;
    return s ? reinterpret_cast<const char*>(s) : "";
}

// FNV-1a over 8 bytes at a time, mixed: enough to tell a frame from the last one, and quick.
void hashBytes(std::uint64_t& h, const void* data, std::size_t size) {
    const auto* p = static_cast<const unsigned char*>(data);
    std::size_t i = 0;
    for (; i + 8 <= size; i += 8) {
        std::uint64_t word;
        std::memcpy(&word, p + i, 8);
        h = (h ^ word) * 0x100000001b3ULL;
        h ^= h >> 29;
    }
    for (; i < size; ++i) h = (h ^ p[i]) * 0x100000001b3ULL;
}

// What a frame draws, hashed: its size, every draw list, and whether a texture is to be made or changed (0 then: a
// frame to draw).
std::uint64_t frameHash(const ImDrawData* draw_data, int fb_width, int fb_height) {
    if (draw_data->Textures) {
        for (const ImTextureData* texture : *draw_data->Textures) {
            if (texture->Status != ImTextureStatus_OK) return 0;
        }
    }
    std::uint64_t h   = 0xcbf29ce484222325ULL;
    const int size[2] = {fb_width, fb_height};
    hashBytes(h, size, sizeof(size));
    for (const ImDrawList* list : draw_data->CmdLists) {
        hashBytes(h, list->VtxBuffer.Data, static_cast<std::size_t>(list->VtxBuffer.size_in_bytes()));
        hashBytes(h, list->IdxBuffer.Data, static_cast<std::size_t>(list->IdxBuffer.size_in_bytes()));
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            const ImTextureID texture = cmd.GetTexID();
            hashBytes(h, &cmd.ClipRect, sizeof(cmd.ClipRect));
            hashBytes(h, &texture, sizeof(texture));
            const unsigned int counts[3] = {cmd.VtxOffset, cmd.IdxOffset, cmd.ElemCount};
            hashBytes(h, counts, sizeof(counts));
        }
    }
    return h == 0 ? 1 : h;
}

// Whether the user is doing something this frame: the mouse moving, a button, the wheel, a key held or typed.
bool userActive() {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f || io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f) {
        return true;
    }
    if (!io.InputQueueCharacters.empty() || ImGui::IsAnyItemActive()) return true;
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
        if (ImGui::IsKeyDown(static_cast<ImGuiKey>(key))) return true;
    }
    return false;
}

// A vertex for SDL_RenderGeometryRaw: colour and texture coordinates in one buffer, with one stride (see drawSdl).
struct SdlVertex {
    float x = 0, y = 0;
    SDL_FColor colour{};
    float u = 0, v = 0;
};

// Dear ImGui's draw lists through SDL's renderer, as ImGui_ImplSDLRenderer3_RenderDrawData does, but with every vertex
// in one buffer. SDL's software driver draws a rectangle (the backgrounds, each letter) at once when it recognizes one,
// as two triangles otherwise, testing every pixel of the box around each; it reads the texture coordinates with the
// colours' stride (SDL 3.4), so with the backend's separate colour buffer it recognized none, and a frame took four
// times longer. The positions are scaled to pixels here, SDL's render scale left at 1.
void drawSdl(ImDrawData* draw_data) {
    const ImVec2 scale  = draw_data->FramebufferScale;
    const int fb_width  = static_cast<int>(draw_data->DisplaySize.x * scale.x);
    const int fb_height = static_cast<int>(draw_data->DisplaySize.y * scale.y);
    if (fb_width <= 0 || fb_height <= 0) return;

    // The font atlas, created or updated by the backend.
    if (draw_data->Textures) {
        for (ImTextureData* texture : *draw_data->Textures) {
            if (texture->Status != ImTextureStatus_OK) ImGui_ImplSDLRenderer3_UpdateTexture(texture);
        }
    }

    SDL_SetRenderViewport(g.renderer, nullptr);
    SDL_SetRenderClipRect(g.renderer, nullptr);
    static std::vector<SdlVertex> vertices;
    const ImVec2 origin = draw_data->DisplayPos;
    for (const ImDrawList* list : draw_data->CmdLists) {
        vertices.resize(static_cast<std::size_t>(list->VtxBuffer.Size));
        for (int i = 0; i < list->VtxBuffer.Size; ++i) {
            const ImDrawVert& in                  = list->VtxBuffer[i];
            const ImVec4 c                        = ImGui::ColorConvertU32ToFloat4(in.col);
            vertices[static_cast<std::size_t>(i)] = {(in.pos.x - origin.x) * scale.x, (in.pos.y - origin.y) * scale.y,
                                                     SDL_FColor{c.x, c.y, c.z, c.w}, in.uv.x, in.uv.y};
        }
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) {
                if (cmd.UserCallback == ImDrawCallback_ResetRenderState) {
                    SDL_SetRenderClipRect(g.renderer, nullptr);
                } else {
                    cmd.UserCallback(list, &cmd);
                }
                continue;
            }
            const float x0 = std::max(0.0f, (cmd.ClipRect.x - origin.x) * scale.x);
            const float y0 = std::max(0.0f, (cmd.ClipRect.y - origin.y) * scale.y);
            const float x1 = std::min(static_cast<float>(fb_width), (cmd.ClipRect.z - origin.x) * scale.x);
            const float y1 = std::min(static_cast<float>(fb_height), (cmd.ClipRect.w - origin.y) * scale.y);
            if (x1 <= x0 || y1 <= y0) continue;
            const SDL_Rect clip{static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(x1 - x0),
                                static_cast<int>(y1 - y0)};
            SDL_SetRenderClipRect(g.renderer, &clip);

            const SdlVertex* first = vertices.data() + cmd.VtxOffset;
            auto* texture          = reinterpret_cast<SDL_Texture*>(static_cast<std::uintptr_t>(cmd.GetTexID()));
            SDL_RenderGeometryRaw(g.renderer, texture, &first->x, sizeof(SdlVertex), &first->colour, sizeof(SdlVertex),
                                  &first->u, sizeof(SdlVertex), list->VtxBuffer.Size - static_cast<int>(cmd.VtxOffset),
                                  list->IdxBuffer.Data + cmd.IdxOffset, static_cast<int>(cmd.ElemCount),
                                  sizeof(ImDrawIdx));
        }
    }
    SDL_SetRenderClipRect(g.renderer, nullptr);
}

// The window with an OpenGL 3.2 core context. False, with the reason in `why`, when the system has none.
bool openGl(const char* title, int width, int height, SDL_WindowFlags flags, std::string& why) {
#ifdef __APPLE__
    // macOS gives a 3.2 core context (up to 4.1) only to a program that asks for a forward-compatible one.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow(title, width, height, flags | SDL_WINDOW_OPENGL);
    if (!window) {
        why = fmt::format("SDL_CreateWindow: {}", SDL_GetError());
        return false;
    }
    // 3.3 for the 3D board (board_3d.hpp), which some drivers give only to a program asking for it; else 3.2.
    SDL_GLContext context = nullptr;
    for (const int minor : {3, 2}) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
        if ((context = SDL_GL_CreateContext(window))) break;
    }
    if (!context) {
        why = fmt::format("SDL_GL_CreateContext: {}", SDL_GetError());
        SDL_DestroyWindow(window);
        return false;
    }
    const auto fail = [&](std::string reason) {
        why = std::move(reason);
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        return false;
    };
    SDL_GL_MakeCurrent(window, context);
    if (!gl::load()) return fail(TR("OpenGL functions are missing"));
    // SDL asked for 3.2; a driver giving less anyway would draw nothing.
    const auto version = glString(gl::VERSION);
    if (!glVersionAtLeast(version.c_str(), 3, 2)) return fail(trf("the driver gives OpenGL {}", version));
    if (!ImGui_ImplOpenGL3_InitLoader()) return fail(TR("OpenGL functions are missing"));
    // No vsync: on Wayland a hidden window would block in SwapWindow. The loop paces itself.
    SDL_GL_SetSwapInterval(0);

    g.path        = Path::OpenGL;
    g.window      = window;
    g.gl_context  = context;
    g.gl_renderer = glString(gl::RENDERER);
    g.gl_version  = version;
    return true;
}

// The window drawn by SDL's renderer: its software driver if `software`, else the first of its drivers that works,
// the software one last. False, with the reason in `why`, when none does.
bool openSdl(const char* title, int width, int height, SDL_WindowFlags flags, bool software, std::string& why) {
    // Those of the OpenGL 3.2 attempt would hold SDL's OpenGL driver to them.
    SDL_GL_ResetAttributes();
    SDL_Window* window = SDL_CreateWindow(title, width, height, flags);
    if (!window) {
        why = fmt::format("SDL_CreateWindow: {}", SDL_GetError());
        return false;
    }

    SDL_Renderer* renderer = nullptr;
    if (!software) {
        std::string drivers;  // in SDL's order of preference, which a comma separated list keeps
        for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i) {
            const char* name = SDL_GetRenderDriver(i);
            if (!name || std::string_view(name) == SDL_SOFTWARE_RENDERER) continue;
            if (!drivers.empty()) drivers += ',';
            drivers += name;
        }
        if (!drivers.empty()) renderer = SDL_CreateRenderer(window, drivers.c_str());
    }
    if (!renderer) {
        // The software driver's picture goes into the window's own pixels where the system gives some (X11 shared
        // memory, GDI on Windows, the offscreen driver), rather than through a graphics card texture, which is what
        // failed or was turned off. Wayland and macOS give none: there SDL needs the graphics card to show it.
        const std::string_view video = SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "";
        if (video == "x11" || video == "windows" || video == "offscreen" || video == "dummy") {
            SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
        }
        renderer = SDL_CreateRenderer(window, SDL_SOFTWARE_RENDERER);
    }
    if (!renderer) {
        why = fmt::format("SDL_CreateRenderer: {}", SDL_GetError());
        SDL_DestroyWindow(window);
        return false;
    }
    SDL_SetRenderVSync(renderer, 0);  // as with OpenGL: the loop paces itself

    const char* name = SDL_GetRendererName(renderer);
    g.path           = Path::Sdl;
    g.window         = window;
    g.renderer       = renderer;
    g.software       = name && std::string_view(name) == SDL_SOFTWARE_RENDERER;
    return true;
}

}  // namespace

SDL_Window* open(const char* title, int width, int height, SDL_WindowFlags flags, bool software) {
    std::string gl_why, sdl_why;
    if (!software && openGl(title, width, height, flags, gl_why)) return g.window;
    if (openSdl(title, width, height, flags, software, sdl_why)) {
        if (!software)
            g.fallback = trf("OpenGL 3.2 cannot be used ({}): the window is drawn by SDL's renderer.", gl_why);
        return g.window;
    }
    // Asked for, the software driver failed: OpenGL then, rather than no window until the setting is changed.
    if (software && openGl(title, width, height, flags, gl_why)) {
        g.fallback = trf("The software renderer cannot be used ({}): the window is drawn with OpenGL.", sdl_why);
        return g.window;
    }

    g.failure = std::string(TR("fastchess-gui cannot draw its window.")) + "\n\nOpenGL 3.2: " + gl_why + "\n" +
                TR("SDL renderer") + ": " + sdl_why + "\n\n" +
                TR("Update the graphics driver, or turn on 3D acceleration in the virtual machine or the remote "
                   "desktop's settings.");
    const char* video = SDL_GetCurrentVideoDriver();
    if (video && std::string_view(video) == "wayland") {
        g.failure += std::string("\n") + TR("Under Wayland the software renderer needs X11: start the program with "
                                            "the environment variable SDL_VIDEODRIVER=x11.");
    }
    return nullptr;
}

void initBackends() {
    if (g.path == Path::OpenGL) {
        ImGui_ImplSDL3_InitForOpenGL(g.window, g.gl_context);
        ImGui_ImplOpenGL3_Init("#version 150");
    } else if (g.path == Path::Sdl) {
        ImGui_ImplSDL3_InitForSDLRenderer(g.window, g.renderer);
        ImGui_ImplSDLRenderer3_Init(g.renderer);
        // Dear ImGui draws thin lines (borders, separators) as rectangles textured with lines drawn in its font
        // atlas. SDL's software driver takes such a rectangle, whose texture coordinates have no height, for a plain
        // one, and draws it wrong (or not at all): lines made of triangles instead.
        ImGui::GetIO().Fonts->Flags |= ImFontAtlasFlags_NoBakedLines;
    }
    g.backends = g.path != Path::None;
}

bool viewports() { return g.path == Path::OpenGL; }

void newFrame() {
    if (g.path == Path::OpenGL) {
        ImGui_ImplOpenGL3_NewFrame();
    } else if (g.path == Path::Sdl) {
        ImGui_ImplSDLRenderer3_NewFrame();
    }
    ImGui_ImplSDL3_NewFrame();
}

void draw(int fb_width, int fb_height) {
    constexpr float R = 0.08f, G = 0.09f, B = 0.10f;
    if (g.path == Path::OpenGL) {
        const auto& f = gl::fn();
        f.Viewport(0, 0, fb_width, fb_height);
        f.ClearColor(R, G, B, 1.0f);
        f.Clear(gl::COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    } else if (g.path == Path::Sdl) {
        ImDrawData* draw_data = ImGui::GetDrawData();
        // The software driver keeps the last frame in the window's pixels, and drawing one costs the processor: a
        // frame like the last one, most of them on a page where nothing moves, is not drawn again, and what moves
        // on its own (clocks, boards, charts) is drawn 15 times a second rather than 30 while the user does
        // nothing. (Other drivers may lose the frame shown, and draw on the graphics card.)
        if (g.software) {
            const auto hash = frameHash(draw_data, fb_width, fb_height);
            const auto now  = std::chrono::steady_clock::now();
            if (hash != 0 && hash == g.last_frame) return;
            if (hash != 0 && now - g.last_draw < std::chrono::milliseconds(66) && !userActive()) return;
            g.last_frame = hash;
            g.last_draw  = now;
        }
        SDL_SetRenderDrawColorFloat(g.renderer, R, G, B, 1.0f);
        SDL_RenderClear(g.renderer);
        drawSdl(draw_data);
    }
}

void drawViewports() {
    if (g.path != Path::OpenGL || !(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) return;
    SDL_Window* current_window    = SDL_GL_GetCurrentWindow();
    SDL_GLContext current_context = SDL_GL_GetCurrentContext();
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    SDL_GL_MakeCurrent(current_window, current_context);
}

void present() {
    if (g.path == Path::OpenGL) {
        SDL_GL_SwapWindow(g.window);
    } else if (g.path == Path::Sdl) {
        SDL_RenderPresent(g.renderer);
    }
}

bool screenshot(const std::string& file, int fb_width, int fb_height) {
    if (g.path == Path::OpenGL) {
        if (fb_width <= 0 || fb_height <= 0) return false;
        std::vector<unsigned char> pixels(static_cast<std::size_t>(fb_width) * fb_height * 4);
        gl::fn().PixelStorei(gl::PACK_ALIGNMENT, 1);
        gl::fn().ReadPixels(0, 0, fb_width, fb_height, gl::RGBA, gl::UNSIGNED_BYTE, pixels.data());
        return savePng(file, fb_width, fb_height, pixels.data(), fb_width * 4, true);  // OpenGL rows go up
    }
    if (g.path != Path::Sdl) return false;
    SDL_Surface* read = SDL_RenderReadPixels(g.renderer, nullptr);
    if (!read) return false;
    SDL_Surface* rgba = SDL_ConvertSurface(read, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(read);
    if (!rgba) return false;
    const bool ok = savePng(file, rgba->w, rgba->h, rgba->pixels, rgba->pitch, false);
    SDL_DestroySurface(rgba);
    return ok;
}

void shutdown() {
    if (g.backends) {
        if (g.path == Path::OpenGL) ImGui_ImplOpenGL3_Shutdown();
        if (g.path == Path::Sdl) ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
    }
    if (g.renderer) SDL_DestroyRenderer(g.renderer);
    if (g.gl_context) SDL_GL_DestroyContext(g.gl_context);
    if (g.window) SDL_DestroyWindow(g.window);
    g = State{};
}

Path path() { return g.path; }

bool software() { return g.software; }

std::string description() {
    if (g.path == Path::OpenGL) return fmt::format("OpenGL · {} · {}", g.gl_renderer, g.gl_version);
    if (g.path == Path::Sdl) {
        const char* name = SDL_GetRendererName(g.renderer);
        return trf("SDL renderer, {} driver", name ? name : "?");
    }
    return {};
}

const std::string& fallbackReason() { return g.fallback; }

const std::string& failure() { return g.failure; }

void logPath() {
    if (!g.fallback.empty()) Console::instance().add(g.fallback, true);
    Console::instance().add(trf("Drawn with: {}", description()));
}

ImTextureID createTexture(int width, int height, const unsigned char* rgba) {
    if (width <= 0 || height <= 0 || !rgba) return ImTextureID_Invalid;
    if (g.path == Path::OpenGL) {
        const auto& f      = gl::fn();
        gl::GLuint texture = 0;
        f.GenTextures(1, &texture);
        if (texture == 0) return ImTextureID_Invalid;
        f.BindTexture(gl::TEXTURE_2D, texture);
        f.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR_MIPMAP_LINEAR);
        f.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
        f.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
        f.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
        f.PixelStorei(gl::UNPACK_ALIGNMENT, 1);
        f.TexImage2D(gl::TEXTURE_2D, 0, static_cast<gl::GLint>(gl::RGBA), width, height, 0, gl::RGBA, gl::UNSIGNED_BYTE,
                     rgba);
        f.GenerateMipmap(gl::TEXTURE_2D);
        f.BindTexture(gl::TEXTURE_2D, 0);
        return static_cast<ImTextureID>(texture);
    }
    if (g.path == Path::Sdl) {
        SDL_Texture* texture =
            SDL_CreateTexture(g.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width, height);
        if (!texture) return ImTextureID_Invalid;
        SDL_UpdateTexture(texture, nullptr, rgba, width * 4);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture));
    }
    return ImTextureID_Invalid;
}

void destroyTexture(ImTextureID texture) {
    if (texture == ImTextureID_Invalid) return;
    if (g.path == Path::OpenGL && gl::fn().DeleteTextures) {
        const auto name = static_cast<gl::GLuint>(texture);
        gl::fn().DeleteTextures(1, &name);
    } else if (g.path == Path::Sdl) {
        SDL_DestroyTexture(reinterpret_cast<SDL_Texture*>(static_cast<std::uintptr_t>(texture)));
    }
}

bool mipmapped() { return g.path == Path::OpenGL; }

bool glVersionAtLeast(const char* version, int major, int minor) {
    int got_major = 0, got_minor = 0;
    if (!version || std::sscanf(version, "%d.%d", &got_major, &got_minor) != 2) return false;
    return got_major > major || (got_major == major && got_minor >= minor);
}

}  // namespace fcg::render
