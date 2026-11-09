#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <future>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <matchmaking/sprt/sprt.hpp>
#include <matchmaking/stats.hpp>

#include "board_view.hpp"
#include "broadcast.hpp"
#include "console.hpp"
#include "dialogs.hpp"
#include "engine_import.hpp"
#include "engines.hpp"
#include "live.hpp"
#include "model.hpp"
#include "pgn_store.hpp"
#include "run.hpp"
#include "stats.hpp"
#include "storage.hpp"
#include "topology.hpp"
#include "ui.hpp"

#include <tour/standings.hpp>

struct SDL_Window;

namespace fcg {

// Plots a White point of view evaluation curve (one value per ply, in pawns).
// Returns the ply clicked in the plot, or -1.
int plotEval(const char* id, const std::vector<float>& evals, int marker_ply, float height);

struct AppOptions {
    bool autostart = false;
    std::string focus_window;  // page or tab shown at startup (for screenshots)
    float ui_scale = 1.0f;     // DPI scale, to rebuild the style when the theme changes
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
    // One view per concurrent game, shared by the mosaic, the focus panel and detached windows.
    struct SlotView {
        SlotState state;
        chess::Board board;
        uint64_t board_version = ~0ULL;
        bool flip              = false;
        std::size_t followed   = 0;  // moves shown, to keep the move list scrolled to the end
        int follow_frames      = 0;
    };

    enum class Page { Home, NewMatch, Tournament, Engines, Log, Settings };
    enum class Tab { Overview, Standings, Schedule, Live, Games, Broadcast };

    // shell
    void drawShell();
    void drawRail(float width);
    void drawHome();
    void drawNewMatch();
    void drawWorkspace();
    void drawWorkspaceHeader();
    void drawSettings();
    void applyThemeSetting();
    void refreshTournaments();
    void openTournament(const std::filesystem::path& dir);
    void importLegacyRuns();

    // views
    void drawSetup();
    void drawLive();
    void refreshSlot(int slot);
    void drawLiveGame(SlotView& view, const char* id, bool allow_detach, int slot);
    void drawMosaic(float width);
    void drawDetached();
    void drawGames();
    void drawResults();
    void drawCharts();
    void drawEngines();
    void drawConsole();
    void drawQuitModal();

    // tournaments
    [[nodiscard]] bool isMatch() const;
    void refreshTable();
    void drawTournamentOverview();
    void drawStandings();
    void drawStandingsTable(bool compact);
    void drawCrosstable();
    void drawSchedule();
    void drawEditPanel();
    void applyTournamentEdit(const tour::Edit& edit);

    // broadcast
    void updateBroadcast();
    [[nodiscard]] BroadcastSnapshot makeSnapshot();
    void drawBroadcast();

    // setup sections
    void setupFormat();
    void setupParticipants();
    void drawSetupSummary();
    void drawTemplates();
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
    void startCheck(int index);
    void pollCheck();
    void importEngines(const ImportResult& imported);
    [[nodiscard]] bool engineInUse(const std::string& name) const;
    void addEngine(const std::string& path);
    std::string uniqueEngineName(const std::string& base, int ignore_index) const;

    // run lifecycle
    void startRun();
    void resumeRun();
    void enqueue();         // the current setup, to start after the running tournament
    void startFromQueue();  // the first queued tournament, if any
    void drawQueue();
    void drawRelaunch();  // "Relaunch with…" of a finished match
    void resetRunView();  // forgets the games and statistics shown, before a run starts
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
    std::vector<EngineTotals> totals_;
    std::chrono::steady_clock::time_point last_totals_{};
    double games_per_hour_      = 0;
    std::size_t games_at_start_ = 0;           // games a resumed match already had
    std::optional<tour::Summary> unfinished_;  // the last match, if it can be resumed
    std::chrono::steady_clock::time_point last_rate_update_{};

    // Live view.
    int live_slot_ = 0;
    std::vector<SlotView> slot_views_;
    std::set<int> detached_;  // slots shown in their own window
    bool live_mosaic_ = true;

    // Games view.
    int games_filter_  = 0;
    int games_subject_ = -1;  // a tournament: participant index whose games are shown, -1 = all
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
    std::future<CheckResult> check_;
    int check_index_ = -1;
    std::string engine_search_;
    std::string engine_group_;  // shown group, empty = all

    // Console view.
    std::vector<Console::Line> console_lines_;
    uint64_t console_version_ = 0;
    bool console_follow_      = true;

    // Standings of a round robin or gauntlet, recomputed when games arrive.
    tour::Table table_;
    std::size_t table_for_ = ~std::size_t(0);
    std::optional<tour::Anchor> anchor_;
    int crosstable_mode_ = 0;                             // 0 score, 1 wins/draws/losses, 2 Elo difference
    std::optional<std::pair<int, int>> crosstable_cell_;  // participant ids (row, column)
    std::string template_name_;

    // Schedule tab and edits.
    int edit_kind_ = 0;        // tour::Edit::Kind
    std::string edit_engine_;  // engine list entry to add or bring in
    bool edit_catch_up_     = true;
    int edit_participant_   = -1;  // participant id
    bool edit_void_played_  = false;
    int edit_cycles_        = 0;
    int edit_replay_filter_ = 0;  // 0 losses on time and crashes, 1 every game of a participant
    std::string edit_message_;
    bool edit_error_ = false;

    // Queue and relaunch.
    std::vector<QueuedTournament> queue_;
    std::string relaunch_engine_;

    // Broadcast.
    TlcsServer tlcs_;
    WebServer web_;
    std::chrono::steady_clock::time_point last_snapshot_{};
    std::chrono::steady_clock::time_point last_files_{};
    std::string files_status_;
    std::string tlcs_error_;
    std::string web_error_;

    // Shell.
    Page page_ = Page::Home;
    std::optional<Tab> tab_request_;  // tab to select on the next frame
    std::vector<tour::Summary> tournaments_;
    bool tournaments_dirty_  = true;
    std::size_t legacy_runs_ = 0;
    std::optional<ui::Theme> theme_applied_;

    // Messages and state.
    std::string start_error_;
    std::string finish_message_;
    std::string window_title_;
    bool quit_            = false;
    bool quit_pending_    = false;
    bool open_quit_modal_ = false;
    bool focus_done_      = false;
    bool autostart_done_  = false;
};

}  // namespace fcg
