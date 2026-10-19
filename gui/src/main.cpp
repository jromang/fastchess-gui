// fastchess-gui: a small desktop front end for the fastchess tournament manager.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <implot.h>

#ifndef _WIN32
#    include <csignal>
#endif

#include <fmt/include/fmt/core.h>
#include <core/config/config.hpp>

#include "app.hpp"
#include "console.hpp"
#include "engines.hpp"
#include "gl.hpp"
#include "i18n.hpp"
#include "screenshot.hpp"
#include "storage.hpp"
#include "topology.hpp"
#include "ui.hpp"

namespace fastchess {
const char* version = "alpha 1.8.2 gui ";
}

namespace fcg::assets {
extern const unsigned char fallback_font[];
extern const std::size_t fallback_font_size;
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
        else if (a == "--size")
            std::sscanf(next().c_str(), "%dx%d", &args.width, &args.height);
        else if (a == "--help" || a == "-h") {
            std::printf(
                "fastchess-gui [--profile DIR] [--autostart] [--size WxH]\n"
                "              [--shot FILE.png[@WINDOW[@SECONDS]]]... [--select-game N] [--focus WINDOW]\n"
                "              [--quit-after SECONDS]\n");
            std::exit(0);
        }
    }
    return args;
}

// A proportional system font with accents, and a symbol font merged in for the few signs we use.
void loadFonts(float size) {
    ImGuiIO& io = ImGui::GetIO();

#ifdef _WIN32
    const char* main_fonts[]     = {"C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\tahoma.ttf",
                                    "C:\\Windows\\Fonts\\arial.ttf"};
    const char* fallback_fonts[] = {"C:\\Windows\\Fonts\\seguisym.ttf"};
#else
    const char* main_fonts[]     = {"/usr/share/fonts/noto/NotoSans-Regular.ttf",
                                    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
                                    "/usr/share/fonts/TTF/DejaVuSans.ttf",
                                    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                    "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
                                    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf"};
    const char* fallback_fonts[] = {"/usr/share/fonts/TTF/DejaVuSans.ttf",
                                    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"};
#endif

    ImFont* font = nullptr;
    for (const char* path : main_fonts) {
        std::error_code ec;
        if (fs::is_regular_file(path, ec)) {
            font = io.Fonts->AddFontFromFileTTF(path, size);
            if (font) break;
        }
    }

    if (!font) {
        // The embedded font: Latin with the French ligatures and punctuation.
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        font = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(fcg::assets::fallback_font),
                                              static_cast<int>(fcg::assets::fallback_font_size), size, &config);
        if (!font) {
            io.Fonts->AddFontDefault();
            return;
        }
    }

    for (const char* path : fallback_fonts) {
        std::error_code ec;
        if (fs::is_regular_file(path, ec)) {
            ImFontConfig config;
            config.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(path, size, &config);
            break;
        }
    }
}

// Startup failures are reported in a message box too: a Windows GUI program has no console.
int fatal(const std::string& what) {
    const auto message = what + ": " + SDL_GetError();
    std::fprintf(stderr, "%s\n", message.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "fastchess-gui", message.c_str(), nullptr);
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    const auto args = parseArgs(argc, argv);

#ifndef _WIN32
    // A write to an engine that just died must not kill the GUI.
    std::signal(SIGPIPE, SIG_IGN);
#endif

    fcg::Console::instance().attach();

    // Engine probes read timeouts from the global configuration before any run exists.
    fastchess::config::TournamentConfig               = std::make_unique<fastchess::config::Tournament>();
    fastchess::config::TournamentConfig->startup_time = fcg::ENGINE_STARTUP_TIME;

    if (!SDL_Init(SDL_INIT_VIDEO)) return fatal("SDL_Init");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    const float scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    const int width   = args.width > 0 ? args.width : static_cast<int>(1440 * scale);
    const int height  = args.height > 0 ? args.height : static_cast<int>(900 * scale);

    SDL_Window* window =
        SDL_CreateWindow("fastchess-gui", width, height,
                         SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) return fatal("SDL_CreateWindow");

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) return fatal("OpenGL 3.2 indisponible (SDL_GL_CreateContext)");
    SDL_GL_MakeCurrent(window, gl_context);
    // No vsync: on Wayland a hidden window would block in SwapWindow. The loop paces itself.
    SDL_GL_SetSwapInterval(0);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    if (!fcg::gl::load()) return fatal("Fonctions OpenGL manquantes");

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

    fcg::ui::applyTheme(scale);
    ImGui::GetStyle().FontScaleDpi = scale;
    loadFonts(16.0f);

    ImPlot::StyleColorsDark();
    ImPlot::GetStyle().PlotPadding = ImVec2(6, 6);

    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 150");

    fcg::AppOptions options;
    options.autostart    = args.autostart;
    options.focus_window = args.focus;

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
                if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                    event.window.windowID == SDL_GetWindowID(window)) {
                    app.requestQuit();
                }
            }

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
