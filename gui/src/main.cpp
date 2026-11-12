// fastchess-gui: a small desktop front end for the fastchess tournament manager.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#ifdef _WIN32
#    include <windows.h>
#endif

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <implot.h>

#ifndef _WIN32
#    include <csignal>
#endif

#include <fmt/include/fmt/format.h>
#include <core/config/config.hpp>
#include <engine/uci_engine.hpp>

#include "app.hpp"
#include "console.hpp"
#include "engines.hpp"
#include "gl.hpp"
#include "headless.hpp"
#include "i18n.hpp"
#include "logo.hpp"
#include "screenshot.hpp"
#include "storage.hpp"
#include "topology.hpp"
#include "ui.hpp"
#include "wb2uci.hpp"

namespace fastchess {
const char* version = "alpha 1.8.3 gui ";
}

namespace fcg::assets {
extern const unsigned char inter_regular[];
extern const std::size_t inter_regular_size;
extern const unsigned char inter_semibold[];
extern const std::size_t inter_semibold_size;
extern const unsigned char lucide_icons[];
extern const std::size_t lucide_icons_size;
}  // namespace fcg::assets

namespace {

namespace fs = std::filesystem;

// --shot FILE[@WINDOW[@SECONDS]], repeatable ('@' because ':' appears in Windows paths).
struct Shot {
    std::string file;
    std::string window;
    double at    = 5.0;
    bool taken   = false;
    bool focused = false;
};

struct Args {
    std::string profile;
    bool autostart = false;
    std::vector<Shot> shots;
    int select_game = -1;
    std::string focus;
    double quit_after = 0;
    int width         = 0;
    int height        = 0;
    std::string run;               // --run: a tournament folder or a setup file, played without a window
    std::optional<int> tlcs_port;  // --run: broadcast options
    bool tlcs_lanes = false;
    std::optional<int> web_port;
    std::optional<std::string> live_dir;  // empty = "live" in the tournament folder
    std::optional<std::string> lichess;   // a lichess round, or "new"
};

Args parseArgs(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next           = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };

        if (a == "--profile")
            args.profile = next();
        else if (a == "--autostart")
            args.autostart = true;
        else if (a == "--shot") {
            const auto spec = next();
            Shot shot;
            const auto c1 = spec.find('@');
            shot.file     = spec.substr(0, c1);
            if (c1 != std::string::npos) {
                const auto c2 = spec.find('@', c1 + 1);
                shot.window   = spec.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
                if (c2 != std::string::npos) shot.at = std::atof(spec.c_str() + c2 + 1);
            }
            args.shots.push_back(shot);
        } else if (a == "--select-game")
            args.select_game = std::atoi(next().c_str());
        else if (a == "--focus")
            args.focus = next();
        else if (a == "--quit-after")
            args.quit_after = std::atof(next().c_str());
        else if (a == "--run")
            args.run = next();
        else if (a == "--tlcs")
            args.tlcs_port = std::atoi(next().c_str());
        else if (a == "--tlcs-lanes")
            args.tlcs_lanes = true;
        else if (a == "--web")
            args.web_port = std::atoi(next().c_str());
        else if (a == "--live-files")
            args.live_dir = i + 1 < argc && argv[i + 1][0] != '-' ? next() : std::string();
        else if (a == "--lichess")
            args.lichess = next();
        else if (a == "--size")
            std::sscanf(next().c_str(), "%dx%d", &args.width, &args.height);
        else if (a == "--help" || a == "-h") {
            std::printf(
                "fastchess-gui [--profile DIR] [--autostart] [--size WxH]\n"
                "              [--shot FILE.png[@WINDOW[@SECONDS]]]... [--select-game N] [--focus WINDOW]\n"
                "              [--quit-after SECONDS]\n"
                "fastchess-gui [--profile DIR] --run TOURNAMENT_FOLDER|SETUP.json   (no window)\n"
                "              [--tlcs UDP_PORT [--tlcs-lanes]] [--web TCP_PORT] [--live-files [DIR]]\n"
                "              [--lichess ROUND_URL|ROUND_ID|new]   (token in FCG_LICHESS_TOKEN)\n");
            std::exit(0);
        }
    }
    return args;
}

ImFont* addEmbedded(const unsigned char* data, std::size_t size, float pixels, bool merge = false) {
    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    config.MergeMode            = merge;
    return ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), static_cast<int>(size), pixels,
                                                      &config);
}

// Inter, with a system font merged in for the few signs it lacks (✓ ✗ ⚠); Inter SemiBold for the titles; Lucide
// on its own for the icons (Inter has glyphs of its own in the private use area Lucide uses).
void loadFonts(float size) {
    using namespace fcg::assets;

#ifdef _WIN32
    const char* symbol_fonts[] = {"C:\\Windows\\Fonts\\seguisym.ttf", "C:\\Windows\\Fonts\\segoeui.ttf"};
#else
    const char* symbol_fonts[] = {"/usr/share/fonts/TTF/DejaVuSans.ttf",
                                  "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                  "/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf",
                                  "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf"};
#endif

    const auto add_symbols = [&] {
        for (const char* path : symbol_fonts) {
            std::error_code ec;
            if (!fs::is_regular_file(path, ec)) continue;
            ImFontConfig config;
            config.MergeMode = true;
            if (ImGui::GetIO().Fonts->AddFontFromFileTTF(path, size, &config)) break;
        }
    };

    ImFont* text = addEmbedded(inter_regular, inter_regular_size, size);
    add_symbols();
    ImFont* titles = addEmbedded(inter_semibold, inter_semibold_size, size);
    add_symbols();
    ImFont* icons = addEmbedded(lucide_icons, lucide_icons_size, size);
    fcg::ui::setFonts(text, titles, icons);
}

// Startup failures are reported in a message box too: a Windows GUI program has no console.
int fatal(const std::string& what) {
    const auto message = what + ": " + SDL_GetError();
    std::fprintf(stderr, "%s\n", message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "fastchess-gui", message.c_str(), nullptr);
    return 1;
}

// This program's own path, to start it again as the WinBoard adapter.
std::string selfPath(const char* argv0) {
#ifdef _WIN32
    char buffer[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return std::string(buffer, n);
#else
    std::error_code ec;
    const auto exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return exe.string();
#endif
    const char* base = SDL_GetBasePath();
    const auto name  = fs::path(argv0).filename();
    return base && !name.empty() ? (fs::path(base) / name).string() : std::string(argv0);
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // The engines inherit it: one missing a DLL (MSVCR100.dll) or crashing fails at once, instead of waiting on a
    // system error box until someone clicks it.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#else
    // A write to an engine that just died must not kill the GUI.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    // A WinBoard engine, run as a UCI one for fastchess: stdin and stdout are the engine's UCI side, untouched.
    if (argc >= 2 && std::string_view(argv[1]) == "--wb2uci") {
        return fcg::wb2uci::run(std::vector<std::string>(argv + 2, argv + argc));
    }

    const auto args = parseArgs(argc, argv);
    fcg::Console::instance().attach();
    fastchess::engine::xboard_adapter = selfPath(argc > 0 ? argv[0] : "");

    // Engine probes read timeouts from the global configuration before any run exists.
    fastchess::config::TournamentConfig               = std::make_unique<fastchess::config::Tournament>();
    fastchess::config::TournamentConfig->startup_time = fcg::ENGINE_STARTUP_TIME;

    // No window: play a tournament from the command line.
    if (!args.run.empty()) {
        const char* pref = SDL_GetPrefPath("fastchess", "fastchess-gui");
        const fcg::Storage storage(args.profile.empty() ? fs::path(pref ? pref : ".") : fs::path(args.profile));
        fcg::HeadlessBroadcast broadcast;
        broadcast.tlcs_port  = args.tlcs_port;
        broadcast.tlcs_lanes = args.tlcs_lanes;
        broadcast.web_port   = args.web_port;
        broadcast.live_dir   = args.live_dir;
        broadcast.lichess    = args.lichess;
        return fcg::runHeadless(args.run, storage, fcg::detectTopology(), broadcast);
    }

#ifndef _WIN32
    // Scripted screenshots render off screen (EGL), so no window shows up on the desktop. SDL_VIDEODRIVER still
    // overrides it.
    if (!args.shots.empty() && !std::getenv("SDL_VIDEODRIVER")) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) return fatal("SDL_Init");
    // Until the setup's language is known (App reads it), startup failures speak the system's.
    fcg::i18n::setLanguage(fcg::i18n::systemLanguage());

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    const float scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    int width         = args.width > 0 ? args.width : static_cast<int>(1440 * scale);
    int height        = args.height > 0 ? args.height : static_cast<int>(900 * scale);

    // The window must fit the screen with its title bar: centred, a window taller than the screen has its title bar
    // (and its close button) above the top edge. When the default size does not fit, it opens maximized.
    std::optional<SDL_Rect> usable;
    bool maximize = false;
    if (args.width <= 0 && args.height <= 0) {
        SDL_Rect bounds;
        if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &bounds) && bounds.w > 0 && bounds.h > 0) {
            const int margin_x = static_cast<int>(32 * scale);
            const int margin_y = static_cast<int>(96 * scale);  // title bar above, some room below
            if (width > bounds.w - margin_x || height > bounds.h - margin_y) {
                width    = std::max(640, std::min(width, bounds.w - margin_x));
                height   = std::max(480, std::min(height, bounds.h - margin_y));
                maximize = true;
            }
            usable = bounds;
        }
    }

    SDL_Window* window =
        SDL_CreateWindow("fastchess-gui", width, height,
                         SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) return fatal("SDL_CreateWindow");
    {
        // The window's icon, with a larger image for high densities and the task switcher.
        auto small = fcg::logo::icon(64), large = fcg::logo::icon(256);
        SDL_Surface* icon = SDL_CreateSurfaceFrom(64, 64, SDL_PIXELFORMAT_RGBA32, small.data(), 64 * 4);
        SDL_Surface* hd   = SDL_CreateSurfaceFrom(256, 256, SDL_PIXELFORMAT_RGBA32, large.data(), 256 * 4);
        if (icon && hd) SDL_AddSurfaceAlternateImage(icon, hd);
        if (icon) SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(hd);
        SDL_DestroySurface(icon);
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) return fatal(TR("OpenGL 3.2 is not available (SDL_GL_CreateContext)"));
    SDL_GL_MakeCurrent(window, gl_context);
    // No vsync: on Wayland a hidden window would block in SwapWindow. The loop paces itself.
    SDL_GL_SetSwapInterval(0);
    if (usable) {
        // Centred in the usable area (not the whole screen, which SDL falls back to), below the title bar.
        SDL_SetWindowPosition(window, usable->x + (usable->w - width) / 2,
                              usable->y + std::max(static_cast<int>(48 * scale), (usable->h - height) / 2));
    } else {
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    SDL_ShowWindow(window);
    if (maximize) SDL_MaximizeWindow(window);

    if (!fcg::gl::load()) return fatal(TR("OpenGL functions are missing"));

    const char* pref = SDL_GetPrefPath("fastchess", "fastchess-gui");
    fcg::Storage storage(args.profile.empty() ? fs::path(pref ? pref : ".") : fs::path(args.profile));
    const auto ini_path = storage.iniFile().string();
    fcg::Console::instance().logTo((storage.root() / "fastchess-gui.log").string());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.IniFilename = ini_path.c_str();

    // Scripted screenshots must not show hover effects from wherever the real mouse happens to be.
    if (!args.shots.empty()) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;

    // A panel of the tournament workspace (a detached board, the standings...) can be dragged out of the main window,
    // onto a second screen. The SDL backend does not support it on Wayland (ImGui then turns the flag off): there,
    // panels float inside the main window.
    if (args.shots.empty()) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    fcg::ui::applyTheme(fcg::ui::Theme::Light, scale);  // the app applies the chosen theme on its first frame
    ImGui::GetStyle().FontScaleDpi = scale;
    loadFonts(16.0f);

    ImPlot::GetStyle().PlotPadding = ImVec2(6, 6);

    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 150");

    fcg::AppOptions options;
    options.autostart    = args.autostart;
    options.focus_window = args.focus;
    options.ui_scale     = scale;

    // The app lives in its own scope so its destructor (which joins the run thread) runs before teardown.
    {
        fcg::App app(window, std::move(storage), fcg::detectTopology(), options);
        if (!app.initGraphics()) fcg::Console::instance().add(TR("Cannot draw the pieces."), true);

        const auto started = std::chrono::steady_clock::now();
        auto shots         = args.shots;
        bool selected      = false;

        while (!app.shouldQuit()) {
            const auto frame_start = std::chrono::steady_clock::now();

            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT) app.requestQuit();
                if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) app.onFocusGained();
                // The scale of the screen the window is on, as at startup: moved to another, or changed in the
                // system's settings.
                if ((event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
                     event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED) &&
                    event.window.windowID == SDL_GetWindowID(window)) {
                    if (const auto display = SDL_GetDisplayForWindow(window)) {
                        app.onDisplayScale(std::max(1.0f, SDL_GetDisplayContentScale(display)));
                    }
                }
                if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                    event.window.windowID == SDL_GetWindowID(window)) {
                    app.requestQuit();
                }
            }

            fcg::ui::loadRequestedFonts(16.0f);  // between frames: the size loadFonts() used
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplSDL3_NewFrame();
                ImGui::NewFrame();
                app.frame();
                ImGui::EndFrame();
                continue;
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            app.frame();

            ImGui::Render();
            int fb_w = 0, fb_h = 0;
            SDL_GetWindowSizeInPixels(window, &fb_w, &fb_h);
            fcg::gl::fn().Viewport(0, 0, fb_w, fb_h);
            fcg::gl::fn().ClearColor(0.08f, 0.09f, 0.10f, 1.0f);
            fcg::gl::fn().Clear(fcg::gl::COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

            if (args.select_game >= 0 && !selected) selected = app.debugSelectGame(args.select_game);

            bool all_taken = !shots.empty();
            for (auto& shot : shots) {
                if (!shot.focused && !shot.window.empty() && elapsed >= shot.at - 0.5) {
                    app.focusWindow(shot.window);
                    shot.focused = true;
                }
                if (!shot.taken && elapsed >= shot.at) {
                    shot.taken    = true;
                    const bool ok = fcg::saveScreenshot(shot.file, fb_w, fb_h);
                    fcg::Console::instance().add(
                        fmt::format("Screenshot {} ({}x{}): {}", shot.file, fb_w, fb_h, ok ? "ok" : "failed"), !ok);
                }
                all_taken &= shot.taken;
            }
            if (all_taken && args.quit_after <= 0) app.forceQuit();
            if (args.quit_after > 0 && elapsed >= args.quit_after) app.forceQuit();

            // Windows dragged out of the main one.
            if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                SDL_Window* current_window    = SDL_GL_GetCurrentWindow();
                SDL_GLContext current_context = SDL_GL_GetCurrentContext();
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
                SDL_GL_MakeCurrent(current_window, current_context);
            }

            SDL_GL_SwapWindow(window);

            // About 30 frames per second is plenty, and leaves the CPU to the engines.
            const auto target = std::chrono::milliseconds(33);
            const auto spent  = std::chrono::steady_clock::now() - frame_start;
            if (spent < target) std::this_thread::sleep_for(target - spent);
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    SDL_GL_DestroyContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
