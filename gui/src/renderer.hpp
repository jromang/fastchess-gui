#pragma once

// What draws the window: OpenGL 3.2 through Dear ImGui's OpenGL backend, or SDL's 2D renderer (SDL_Renderer) where
// OpenGL 3.2 is missing: virtual machines, remote desktops, old servers. SDL's renderer has drivers of its own
// (Direct3D 11 on Windows, OpenGL 2.1, OpenGL ES 2) and a software one, which needs no graphics card at all. It is
// picked when an OpenGL 3.2 context cannot be made, or asked for (--software, the Settings page), and then it is
// the software driver.
//
// One window, so the state is the module's own, as for the OpenGL functions (gl.hpp).

#include <string>

#include <SDL3/SDL_video.h>
#include <imgui.h>

namespace fcg::render {

enum class Path { None, OpenGL, Sdl };

// Creates the main window, `flags` and what the path needs, and its context or renderer: OpenGL 3.2 unless
// `software`, else (or when that fails) SDL's renderer. Returns nullptr when neither works; failure() then says why
// and what to do, for a message box.
SDL_Window* open(const char* title, int width, int height, SDL_WindowFlags flags, bool software);

// Dear ImGui's platform and renderer backends for that window. Needs the ImGui context.
void initBackends();
// Whether panels can be dragged out of the main window: SDL's renderer draws one window only.
[[nodiscard]] bool viewports();

// The frame: before ImGui::NewFrame(), after ImGui::Render() (the window cleared, then the UI), the windows dragged
// out of the main one, and the frame shown.
void newFrame();
void draw(int fb_width, int fb_height);
void drawViewports();
void present();
// Saves what draw() drew as a PNG file, between draw() and present().
bool screenshot(const std::string& file, int fb_width, int fb_height);

// The backends, the context or renderer, then the window. Before ImGui::DestroyContext().
void shutdown();

// The path the window is drawn with, None before open().
[[nodiscard]] Path path();
// Whether it is SDL's software driver: every pixel drawn by the processor.
[[nodiscard]] bool software();
// For the Settings page and the log: the path and its driver ("OpenGL · <renderer> · <version>",
// "SDL renderer, software driver").
[[nodiscard]] std::string description();
// Why the path in use is not the one asked for (OpenGL 3.2 failed, or the software driver), a sentence for the log
// and the Settings page; empty when it is.
[[nodiscard]] const std::string& fallbackReason();
// Writes the path in use to the log, after the fallback as a warning.
void logPath();
// Why neither path works, and what to do: the text of the message box when open() returned nullptr.
[[nodiscard]] const std::string& failure();

// A texture of `width` x `height` RGBA pixels (rows from the top, 4 bytes a pixel, no padding) on the path in use,
// filtered when drawn at another size; ImTextureID_Invalid when it cannot be made. OpenGL has mipmaps for smaller
// sizes, SDL's renderer has none: see mipmapped().
[[nodiscard]] ImTextureID createTexture(int width, int height, const unsigned char* rgba);
void destroyTexture(ImTextureID texture);
// Whether a texture drawn much smaller than its size stays smooth. When not, smaller images look better made by the
// caller.
[[nodiscard]] bool mipmapped();

// Whether an OpenGL version string ("4.6 (Core Profile) Mesa 24.2", "3.1.0 - Build ...") is at least major.minor.
// OpenGL ES strings ("OpenGL ES 3.2 ...") are not desktop OpenGL: false.
[[nodiscard]] bool glVersionAtLeast(const char* version, int major, int minor);

}  // namespace fcg::render
