#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <future>
#include <optional>
#include <string>
#include <vector>

#include <matchmaking/sprt/sprt.hpp>
#include <matchmaking/stats.hpp>

#include "board_view.hpp"
#include "console.hpp"
#include "dialogs.hpp"
#include "engines.hpp"
#include "live.hpp"
#include "model.hpp"
#include "pgn_store.hpp"
#include "run.hpp"
#include "stats.hpp"
#include "storage.hpp"
#include "topology.hpp"

struct SDL_Window;

namespace fcg {

// Plots a White point of view evaluation curve (one value per ply, in pawns).
// Returns the ply clicked in the plot, or -1.
int plotEval(const char* id, const std::vector<float>& evals, int marker_ply, float height);

struct AppOptions {
    bool autostart = false;
    std::string focus_window;  // window brought to front at startup (for screenshots)
};

class App {
   public:
    App(SDL_Window* window, Storage storage, CpuTopology topology, AppOptions options);
    ~App();

    // Builds one frame of UI. Call between ImGui::NewFrame() and ImGui::Render().
    void frame();

    // The window asked to close: confirm first if a match is running.
    void requestQuit();
    [[nodiscard]] bool shouldQuit() const noexcept { return quit_; }

    // Stops a running match without asking (used by --quit-after).
    void forceQuit();

    // For scripted screenshots.
    void focusWindow(const std::string& name);
    bool debugSelectGame(int index);

    [[nodiscard]] bool initGraphics();

   private:
    // views
    void drawToolbar();
    void drawSetup();
    void drawLive();
    void drawGames();
    void drawResults();
    void drawCharts();
    void drawEngines();
    void drawConsole();
    void drawQuitModal();

    void buildDefaultLayout(unsigned int dockspace);

    // setup sections
    void setupEngines();
    void setupMatch();
    void setupLimit();
    void setupConcurrency();
    void setupOpenings();
    void setupAdjudication();
    void setupPgn();

    // engines
    void startProbe(int index);
    void pollProbe();
    void addEngine(const std::string& path);
    std::string uniqueEngineName(const std::string& base, int ignore_index) const;

    // run lifecycle
    void startRun();
    void pollRun();
    void onRunFinished();
    std::string runStatusText() const;
    std::string summaryText() const;

    // games
    void rebuildGameFilter();
    void selectGame(int index);
    void exportGames(const std::string& path, bool all);

    void handleDialogs();
    void saveSetupSoon() { setup_dirty_ = true; }

    SDL_Window* window_;
    Storage storage_;
    CpuTopology topology_;
    AppOptions options_;

    std::vector<EngineEntry> engines_;
    MatchSetup setup_;
    bool setup_dirty_ = false;
    std::chrono::steady_clock::time_point last_setup_save_{};

    RunController run_;
    Dialogs dialogs_;
    PieceAtlas atlas_;

    // Data copied from the observer of the current (or last) run.
    std::vector<GameRecord> games_;
    std::vector<StatPoint> history_;
    fastchess::Stats stats_;
    StatPoint current_;
    std::optional<fastchess::SPRT> sprt_;
    std::vector<std::string> reasons_;
    std::array<EngineTotals, 2> totals_{};
    std::chrono::steady_clock::time_point last_totals_{};
    double games_per_hour_ = 0;
    std::chrono::steady_clock::time_point last_rate_update_{};

    // Live view.
    int live_slot_ = 0;
    SlotState live_;
    uint64_t live_board_version_ = ~0ULL;
    chess::Board live_board_;
    bool flip_live_ = false;

    // Games view.
    int games_filter_ = 0;
    std::vector<int> filtered_;
    std::size_t filtered_for_ = ~std::size_t(0);
    int selected_game_        = -1;
    std::optional<ReplayGame> replay_;
    std::string replay_error_;
    int replay_ply_   = 0;  // number of moves played on the replay board
    bool flip_replay_ = false;

    // Engines view.
    int selected_engine_ = -1;
    std::future<ProbeResult> probe_;
    int probe_index_ = -1;
    std::vector<std::string> probe_queue_;  // engine names waiting for a probe
    std::string probe_message_;
    std::string option_filter_;

    // Console view.
    std::vector<Console::Line> console_lines_;
    uint64_t console_version_ = 0;
    bool console_follow_      = true;

    // Messages and state.
    std::string start_error_;
    std::string finish_message_;
    std::string window_title_;
    bool quit_            = false;
    bool quit_pending_    = false;
    bool open_quit_modal_ = false;
    bool layout_checked_  = false;
    bool focus_done_      = false;
    bool autostart_done_  = false;
};

}  // namespace fcg
