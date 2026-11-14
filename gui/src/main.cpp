// fastchess-gui: a small desktop front end for the fastchess tournament manager.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>

#ifdef _WIN32
#    include <windows.h>
#elif defined(__APPLE__)
#    include <mach-o/dyld.h>
#endif

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <implot.h>

#ifndef _WIN32
#    include <csignal>
#endif

#include <fmt/include/fmt/format.h>
#include <core/config/config.hpp>
#include <engine/uci_engine.hpp>
#include <tour/book_adapter.hpp>
#include <tour/pgn_book.hpp>

#include "app.hpp"
#include "console.hpp"
#include "engines.hpp"
#include "headless.hpp"
#include "i18n.hpp"
#include "logo.hpp"
#include "portable.hpp"
#include "report.hpp"
#include "screenshot.hpp"
#include "renderer.hpp"
#include "storage.hpp"
#include "topology.hpp"
#include "ui.hpp"
#include "wb2uci.hpp"
#include "window_place.hpp"

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
    bool software     = false;     // SDL's software renderer rather than OpenGL
    std::string run;               // --run: a tournament folder or a setup file, played without a window
    std::optional<int> tlcs_port;  // --run: broadcast options
    bool tlcs_lanes = false;
    std::optional<int> web_port;
    std::optional<std::string> live_dir;  // empty = "live" in the tournament folder
    std::optional<std::string> lichess;   // a lichess round, or "new"
    std::optional<std::string> tunnel;    // localhost.run, or ssh://user@host[:port][/remote_port]
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
        else if (a == "--tunnel")
            args.tunnel = i + 1 < argc && argv[i + 1][0] != '-' ? next() : std::string("localhost.run");
        else if (a == "--size")
            std::sscanf(next().c_str(), "%dx%d", &args.width, &args.height);
        else if (a == "--software")
            args.software = true;
        else if (a == "--help" || a == "-h") {
            std::printf(
                "fastchess-gui [--profile DIR] [--autostart] [--size WxH] [--software]\n"
                "              [--shot FILE.png[@WINDOW[@SECONDS]]]... [--select-game N] [--focus WINDOW]\n"
                "              [--quit-after SECONDS]\n"
                "fastchess-gui [--profile DIR] --run TOURNAMENT_FOLDER|SETUP.json   (no window)\n"
                "              [--tlcs UDP_PORT [--tlcs-lanes]] [--web TCP_PORT] [--live-files [DIR]]\n"
                "              [--lichess ROUND_URL|ROUND_ID|new]   (token in FCG_LICHESS_TOKEN)\n"
                "              [--tunnel [localhost.run|ssh://USER@HOST[:PORT][/REMOTE_PORT]]]\n"
                "                  (the web page on the internet through an SSH tunnel; --web's port, else 8080)\n");
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
#elif defined(__APPLE__)
    const char* symbol_fonts[] = {"/System/Library/Fonts/Apple Symbols.ttf",
                                  "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
                                  "/Library/Fonts/Arial Unicode.ttf"};
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

// Startup failures are reported in a message box too: a Windows GUI program has no console. `what` is followed by
// SDL's error unless `sdl_error` is false. It offers to report the problem, as a new GitHub issue filled with the
// message (see report.hpp).
int fatal(const std::string& what, bool sdl_error = true) {
    const auto message = sdl_error ? what + ": " + SDL_GetError() : what;
    std::fprintf(stderr, "%s\n", message.c_str());

    const std::string report_label           = TR("Report a problem…");
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "OK"},
        {0, 1, report_label.c_str()},
    };
    const SDL_MessageBoxData box = {
        SDL_MESSAGEBOX_ERROR, nullptr, "fastchess-gui", message.c_str(), 2, buttons, nullptr};
    int pressed = 0;
    if (SDL_ShowMessageBox(&box, &pressed) && pressed == 1) {
        fcg::report::Facts facts;
        facts.version     = "fastchess-gui " FCG_VERSION;
        const char* video = SDL_GetCurrentVideoDriver();
        facts.system      = "System: " + fcg::report::systemName() + "\nDisplay: " + (video ? video : "?");
        facts.seen        = "At startup: " + message;
        SDL_OpenURL(fcg::report::issueUrl(facts).c_str());
    }
    return 1;
}

// This program's own path, to start it again as the WinBoard adapter or the book adapter.
std::string selfPath(const char* argv0) {
#ifdef _WIN32
    char buffer[MAX_PATH];
    const DWORD n = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return std::string(buffer, n);
#elif defined(__APPLE__)
    // Inside the application bundle: Contents/MacOS/fastchess-gui (SDL_GetBasePath() is Contents/Resources).
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (size > 0 && _NSGetExecutablePath(buffer.data(), &size) == 0) {
        buffer.resize(std::strlen(buffer.c_str()));
        std::error_code ec;
        const auto exe = fs::canonical(buffer, ec);
        return ec ? buffer : exe.string();
    }
#else
    std::error_code ec;
    const auto exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return exe.string();
#endif
    const char* base = SDL_GetBasePath();
    const auto name  = fs::path(argv0).filename();
    return base && !name.empty() ? (fs::path(base) / name).string() : std::string(argv0);
}

// The usable area of every screen (without its task bar), the primary one first.
std::vector<fcg::ScreenRect> usableScreens() {
    std::vector<fcg::ScreenRect> out;
    const auto add = [&](SDL_DisplayID id) {
        SDL_Rect r;
        if (SDL_GetDisplayUsableBounds(id, &r) && r.w > 0 && r.h > 0) out.push_back({r.x, r.y, r.w, r.h});
    };
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    if (primary) add(primary);
    int count               = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    for (int i = 0; displays && i < count; ++i) {
        if (displays[i] != primary) add(displays[i]);
    }
    SDL_free(displays);
    return out;
}

// The main window's size and place while it is neither maximized nor minimized, and whether it is maximized: what
// the next launch opens it as. Read at each frame rather than on its events: some systems tell a maximized
// window's new size before its new state.
class WindowTracker {
   public:
    explicit WindowTracker(SDL_Window* window) : window_(window) {
        const char* driver = SDL_GetCurrentVideoDriver();
        knows_place_       = !driver || std::string_view(driver) != "wayland";
        update();
    }

    void update() {
        const auto flags = SDL_GetWindowFlags(window_);
        if (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) return;
        place_.maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
        if (flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_FULLSCREEN)) return;
        int w = 0, h = 0;
        if (SDL_GetWindowSize(window_, &w, &h) && w > 0 && h > 0) {
            place_.w = w;
            place_.h = h;
        }
        int x = 0, y = 0;
        if (knows_place_ && SDL_GetWindowPosition(window_, &x, &y)) {
            place_.x = x;
            place_.y = y;
        }
    }

    [[nodiscard]] const fcg::WindowPlace& place() const noexcept { return place_; }

   private:
    SDL_Window* window_;
    bool knows_place_ = true;  // Wayland neither tells a window its place nor lets it choose one
    fcg::WindowPlace place_;
};

// The profile folder: --profile's; in the portable mode (a file named "portable" beside the program), "data" beside
// the program, whose paths are then kept relative to its folder; otherwise the system's place for it. Not
// SDL_GetPrefPath in the portable mode: it creates the folder, and that mode writes nothing outside its own.
fs::path profileFolder(const Args& args, const std::string& executable) {
    if (!args.profile.empty()) return fs::path(args.profile);
    if (const auto folder = fcg::portable::detect(executable); !folder.empty()) {
        fcg::portable::setBase(folder);
        return folder / fcg::portable::DATA_FOLDER;
    }
    char* pref      = SDL_GetPrefPath("fastchess", "fastchess-gui");
    const auto path = pref ? fs::path(pref) : fs::path(".");
    SDL_free(pref);
    return path;
}

// The PGN books turned into Polyglot books are kept in the profile; those not used for a month are removed.
void useBookCache(const fcg::Storage& storage) {
    fcg::tour::book_cache_dir = storage.root() / "book-cache";
    (void)fcg::tour::pruneBookCache(30);
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
    // An engine with a book of its own: the same, the book's moves answered by the adapter.
    if (argc >= 2 && std::string_view(argv[1]) == "--book-adapter") {
        return fcg::tour::runBookAdapter(std::vector<std::string>(argv + 2, argv + argc));
    }

    const auto args = parseArgs(argc, argv);
    fcg::Console::instance().attach();
    fastchess::engine::xboard_adapter = selfPath(argc > 0 ? argv[0] : "");
    fcg::tour::book_adapter           = fastchess::engine::xboard_adapter;

    // Engine probes read timeouts from the global configuration before any run exists.
    fastchess::config::TournamentConfig               = std::make_unique<fastchess::config::Tournament>();
    fastchess::config::TournamentConfig->startup_time = fcg::ENGINE_STARTUP_TIME;

    // No window: play a tournament from the command line.
    if (!args.run.empty()) {
        const fcg::Storage storage(profileFolder(args, fastchess::engine::xboard_adapter));
        useBookCache(storage);
        fcg::HeadlessBroadcast broadcast;
        broadcast.tlcs_port  = args.tlcs_port;
        broadcast.tlcs_lanes = args.tlcs_lanes;
        broadcast.web_port   = args.web_port;
        broadcast.live_dir   = args.live_dir;
        broadcast.lichess    = args.lichess;
        broadcast.tunnel     = args.tunnel;
        return fcg::runHeadless(args.run, storage, fcg::detectTopology(), broadcast);
    }

#if !defined(_WIN32) && !defined(__APPLE__)
    // Scripted screenshots render off screen (EGL), so no window shows up on the desktop. SDL_VIDEODRIVER still
    // overrides it. macOS has no EGL: its screenshots are taken in a window.
    if (!args.shots.empty() && !std::getenv("SDL_VIDEODRIVER")) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) return fatal("SDL_Init");
    // Until the setup's language is known (App reads it), startup failures speak the system's.
    fcg::i18n::setLanguage(fcg::i18n::systemLanguage());

    const float scale = std::max(1.0f, SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));

    // The profile before the window: its settings say whether the window is drawn by SDL's software renderer.
    fcg::Storage storage(profileFolder(args, fastchess::engine::xboard_adapter));
    // A copy, for what main() keeps itself once the app has the profile: the window's place.
    const fcg::Storage profile = storage;

    // The window opens as it was left, on a screen still there (see window_place.hpp). Not for scripted screenshots
    // nor a size asked on the command line: those neither use nor change it.
    const bool remember_place = args.width <= 0 && args.height <= 0 && args.shots.empty();
    fcg::WindowOpening opening;
    if (args.width > 0 || args.height > 0) {
        opening.w = args.width > 0 ? args.width : static_cast<int>(1440 * scale);
        opening.h = args.height > 0 ? args.height : static_cast<int>(900 * scale);
    } else {
        opening = fcg::openingPlace(remember_place ? profile.windowPlace() : std::nullopt, usableScreens(), scale);
    }

    // Drawn with OpenGL 3.2, or by SDL's renderer where it is missing or when software rendering is asked for.
    SDL_Window* window = fcg::render::open("fastchess-gui " FCG_VERSION, opening.w, opening.h,
                                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                           args.software || storage.softwareRendering());
    if (!window) return fatal(fcg::render::failure(), false);
#ifndef __APPLE__  // macOS shows the bundle's (gui/mac/fastchess-gui.icns), with the margin of the system's icons
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
#endif

    if (opening.x && opening.y) {
        // In the usable area of a screen (not the whole screen, which SDL centres in), below the title bar.
        SDL_SetWindowPosition(window, *opening.x, *opening.y);
    } else {
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    SDL_ShowWindow(window);
    if (opening.maximize) SDL_MaximizeWindow(window);
    WindowTracker tracker(window);

    useBookCache(storage);
    const auto ini_path = storage.iniFile().string();
    fcg::Console::instance().logTo(storage.root() / "fastchess-gui.log");
    fcg::render::logPath();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.IniFilename = ini_path.c_str();

    // Scripted screenshots must not show hover effects from wherever the real mouse happens to be.
    if (!args.shots.empty()) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;

    // A panel of the tournament workspace (a detached board, the standings...) can be dragged out of the main window,
    // onto a second screen. The SDL backend does not support it on Wayland (ImGui then turns the flag off), nor SDL's
    // renderer, which draws one window: there, panels float inside the main window.
    if (args.shots.empty() && fcg::render::viewports()) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    fcg::ui::applyTheme(fcg::ui::Theme::Light, scale);  // the app applies the chosen theme on its first frame
    ImGui::GetStyle().FontScaleDpi = scale;
    loadFonts(16.0f);

    ImPlot::GetStyle().PlotPadding = ImVec2(6, 6);

    fcg::render::initBackends();

    fcg::AppOptions options;
    options.autostart    = args.autostart;
    options.focus_window = args.focus;
    options.ui_scale     = scale;
    options.scripted     = !args.shots.empty();

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

            tracker.update();
            fcg::ui::loadRequestedFonts(16.0f);  // between frames: the size loadFonts() used
            if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                fcg::render::newFrame();
                ImGui::NewFrame();
                app.frame();
                ImGui::EndFrame();
                continue;
            }

            fcg::render::newFrame();
            ImGui::NewFrame();

            app.frame();

            ImGui::Render();
            int fb_w = 0, fb_h = 0;
            SDL_GetWindowSizeInPixels(window, &fb_w, &fb_h);
            fcg::render::draw(fb_w, fb_h);

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
                    const bool ok = fcg::render::screenshot(shot.file, fb_w, fb_h);
                    fcg::Console::instance().add(
                        fmt::format("Screenshot {} ({}x{}): {}", shot.file, fb_w, fb_h, ok ? "ok" : "failed"), !ok);
                }
                all_taken &= shot.taken;
            }
            if (all_taken && args.quit_after <= 0) app.forceQuit();
            if (args.quit_after > 0 && elapsed >= args.quit_after) app.forceQuit();

            fcg::render::drawViewports();  // windows dragged out of the main one
            fcg::render::present();

            // About 30 frames per second is plenty, and leaves the CPU to the engines.
            const auto target = std::chrono::milliseconds(33);
            const auto spent  = std::chrono::steady_clock::now() - frame_start;
            if (spent < target) std::this_thread::sleep_for(target - spent);
        }
    }

    if (remember_place) profile.setWindowPlace(tracker.place());

    fcg::render::shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_Quit();

    return 0;
}
