#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <matchmaking/sprt/sprt.hpp>
#include <matchmaking/stats.hpp>

#include "board_view.hpp"
#include "broadcaster.hpp"
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
#include "suite_run.hpp"
#include "topology.hpp"
#include "ui.hpp"

#include <tour/book_scan.hpp>
#include <tour/by_opening.hpp>
#include <tour/game_db.hpp>
#include <tour/ratinglist.hpp>
#include <tour/standings.hpp>

struct SDL_Window;

namespace fcg {

// Plots a White point of view evaluation curve (one value per ply, in pawns).
// Returns the ply clicked in the plot, or -1.
int plotEval(const char* id, const std::vector<float>& evals, int marker_ply, float height);

// The win, draw and loss shares engines reported (UCI_ShowWDL), from White's point of view, after the plies that
// have them.
struct WdlSeries {
    std::vector<float> ply;
    std::vector<float> win;       // White wins
    std::vector<float> not_lost;  // White wins or draws
    [[nodiscard]] bool empty() const noexcept { return ply.empty(); }
};
template <typename Moves>
WdlSeries wdlSeries(const Moves& moves) {
    WdlSeries s;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        const auto& m = moves[i];
        if (m.book || !m.wdl) continue;
        const bool white = m.mover == chess::Color::WHITE;
        const float w = static_cast<float>((*m.wdl)[white ? 0 : 2]), d = static_cast<float>((*m.wdl)[1]),
                    l   = static_cast<float>((*m.wdl)[white ? 2 : 0]);
        const float sum = std::max(1.0f, w + d + l);
        s.ply.push_back(static_cast<float>(i + 1));
        s.win.push_back(w / sum);
        s.not_lost.push_back((w + d) / sum);
    }
    return s;
}

// The WdlSeries as three bands, White's wins at the bottom; the x axis is the eval curve's for `plies` plies.
// Returns the ply clicked, or -1.
int plotWdl(const char* id, const WdlSeries& wdl, std::size_t plies, int marker_ply, float height);

// What the chart under a game shows, move by move.
enum class MoveChart { Eval, Time, Depth, Nodes, Speed, TbHits, Agreement };

// Each side's values, at the plies of its moves: [0] White, [1] Black. `extra` is the seldepth beside the depth and
// the decision time beside the move time.
struct MoveSeries {
    std::array<std::vector<float>, 2> ply, value, extra;
    [[nodiscard]] bool empty() const noexcept { return ply[0].empty() && ply[1].empty(); }
};

// How many plies the PV of a move repeats of what the opponent's previous PV predicted after its own move.
[[nodiscard]] int pvAgreement(const std::string& previous_pv, const std::string& pv);

template <typename Moves>
MoveSeries moveSeries(const Moves& moves, MoveChart chart) {
    MoveSeries s;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        const auto& m = moves[i];
        if (m.book) continue;
        float value = 0, extra = std::nanf("");
        switch (chart) {
            case MoveChart::Eval:
                return s;
            case MoveChart::Time:
                if (m.time_ms < 0) continue;
                value = static_cast<float>(m.time_ms) / 1000.0f;
                if (m.decision_ms) extra = static_cast<float>(*m.decision_ms) / 1000.0f;
                break;
            case MoveChart::Depth:
                if (m.depth <= 0) continue;
                value = static_cast<float>(m.depth);
                if (m.seldepth > 0) extra = static_cast<float>(m.seldepth);
                break;
            case MoveChart::Nodes:
                if (m.nodes == 0) continue;
                value = static_cast<float>(m.nodes);
                break;
            case MoveChart::Speed:
                if (m.nps == 0) continue;
                value = static_cast<float>(m.nps);
                break;
            case MoveChart::TbHits:
                value = static_cast<float>(m.tbhits);
                break;
            case MoveChart::Agreement:
                if (i == 0 || moves[i - 1].book || moves[i - 1].pv.empty() || m.pv.empty()) continue;
                value = static_cast<float>(pvAgreement(moves[i - 1].pv, m.pv));
                break;
        }
        const int side = m.mover == chess::Color::WHITE ? 0 : 1;
        s.ply[side].push_back(static_cast<float>(i + 1));
        s.value[side].push_back(value);
        s.extra[side].push_back(extra);
    }
    if (chart == MoveChart::TbHits) {
        const auto none = [](const std::vector<float>& v) {
            return std::all_of(v.begin(), v.end(), [](float x) { return x == 0; });
        };
        if (none(s.value[0]) && none(s.value[1])) return {};
    }
    return s;
}

// The chart choice above the curve; true when it changed.
bool moveChartChoice(const char* id, MoveChart& chart);

// The MoveSeries of both engines, named, over `plies` plies. Returns the ply clicked, or -1.
int plotMoves(const char* id, MoveChart chart, const MoveSeries& series, const std::array<std::string, 2>& names,
              std::size_t plies, int marker_ply, float height);

// "W 12 · D 80 · L 8 %" and a thin bar of the three, for a side's own win, draw, loss in permille.
void wdlLine(const std::array<int64_t, 3>& wdl);

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

    // The window came back to the front: an engine may have been rebuilt or moved meanwhile.
    void onFocusGained() { refreshPrograms(); }

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

    enum class Page { Home, NewMatch, Tournament, Engines, Books, Suites, Ratings, Log, Settings };
    enum class Tab { Overview, Standings, Bracket, Schedule, Live, Games, Openings, Broadcast };

    // shell
    void drawShell();
    void drawRail(float width);
    void drawHome();
    void drawNewMatch();
    void drawWorkspace();
    void drawWorkspaceHeader();
    void drawSettings();
    void drawRatings();
    void drawRatingFilters();
    void drawRatingList();
    void drawEngineCard();
    void drawHeadToHead();
    void drawProgress();
    void refreshRatings();
    [[nodiscard]] tour::GameFilter ratingFilter() const;
    void showEngineCard(const std::string& name);
    void openGame(const tour::DbGame& game);  // opens its tournament on the Games tab, the game selected
    void applyThemeSetting();
    void handleZoomKeys();
    void refreshTournaments();
    void openTournament(const std::filesystem::path& dir);
    void importLegacyRuns();

    // views
    void drawSetup(bool two_columns, bool with_summary);
    void drawLive();
    void refreshSlot(int slot);
    void drawLiveGame(SlotView& view, const char* id, bool allow_detach, int slot);
    void drawMosaic(int cols, float card);
    void drawDetached();
    void drawGames();
    void drawOpenings();
    void refreshOpenings();
    [[nodiscard]] std::vector<const tour::OpeningStats*> shownOpenings() const;  // filtered, in the shown order
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
    void drawPgnOutError();  // when the PGN file of setup "Also append every game to" cannot be written
    void drawCrosstable();
    void drawSchedule();
    void drawBracket();
    void drawEditPanel();
    void applyTournamentEdit(const tour::Edit& edit);
    void undoTournamentEdit();
    void refreshAfterEdit();

    // broadcast
    void updateBroadcast();
    void drawBroadcast();
    void drawLichess(bool& changed);
    void lichessTokenField();    // in the Broadcast tab and in Settings
    std::string lichess_token_;  // from the profile's settings

    // setup sections
    void setupFormat();
    void setupParticipants();
    void drawSetupSummary();
    void drawTemplates();
    void setupEngines();
    void setupMatch();
    void drawSprtCalculator();
    void setupLimit();
    void setupConcurrency();
    void setupOpenings();
    void setupAdjudication();
    void setupPgn();

    // test suites
    void drawSuites();
    void drawSuiteGrid();
    void loadSuiteSetup();  // suite.json and its suite, once
    void loadSuite();
    void openSuite(const std::string& file);  // and shows the Suites page
    void startSuite();
    void exportSuiteCsv(const std::string& path);
    SuiteSetup suite_setup_;
    bool suite_loaded_ = false;  // suite.json read
    std::optional<tour::Suite> suite_;
    std::string suite_error_;
    SuiteRunner suite_runner_;
    bool suite_saved_run_ = true;                                            // the last run was kept
    std::vector<SuiteColumn> suite_saved_;                                   // saved runs added to the grid
    std::vector<std::pair<std::filesystem::path, std::string>> suite_runs_;  // the saved runs of this suite
    std::string suite_csv_;                                                  // waiting for its file name

    // books
    void drawBooks();
    void drawBookDetail(BookEntry& book);
    void drawBookPositions();
    void addBook(const std::string& path);
    void startBookScan(int index);
    void pollBookScan();
    void cancelBookScan();

    // engines
    void refreshPrograms();
    // Reads the engine's options now, or first when another engine is being read.
    void startProbe(int index);
    // This engine's options are being read, or waiting to be.
    [[nodiscard]] bool probing(const std::string& name) const;
    void pollProbe();
    void startCheck(int index);
    void startQuickTest(int index);
    void finishQuickTest();
    void pollCheck();
    void importEngines(const ImportResult& imported);
    [[nodiscard]] bool engineInUse(const std::string& name) const;
    void addEngine(const std::string& path);
    std::string uniqueEngineName(const std::string& base, int ignore_index) const;

    // run lifecycle
    void startRun();
    void resumeRun();
    void enqueue();         // the current setup, to start after the running tournament
    // The first queued tournament, if any; with `go_on`, the next ones when it cannot start. One that cannot
    // start stays queued.
    void startFromQueue(bool go_on = false);
    void drawQueue();
    void drawRelaunch();  // "Relaunch with…" of a finished match
    void resetRunView();  // forgets the games and statistics shown, before a run starts
    void pollRun();
    void onRunFinished();
    std::string runStatusText() const;
    std::string summaryText() const;

    // games
    void rebuildGameFilter();
    [[nodiscard]] bool gamesFiltered() const;  // some games of the Games tab are hidden
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
    std::vector<OpeningName> openings_;
    std::string opening_filter_;  // Games tab: ECO code, range or words of the opening name
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
    bool live_mosaic_       = true;
    MoveChart live_chart_   = MoveChart::Eval;
    MoveChart replay_chart_ = MoveChart::Eval;

    // SPRT calculator: its own copy of the bounds, applied to the setup on request.
    // Schedule tab: the names of the openings its encounters started with, by encounter.
    const tour::TournamentState* schedule_openings_for_ = nullptr;
    std::map<uint32_t, std::string> schedule_openings_;

    bool sprt_calc_open_  = false;
    bool open_statistics_ = false;  // --focus statistics: the match's Statistics section opens
    bool open_pgn_setup_  = false;  // --focus setup-pgn: the setup's PGN section opens
    MatchSetup sprt_calc_;
    // The running tournament's tablebase adjudication limit (see tablebasePieces), read once per tournament.
    int tbPieces();
    const tour::TournamentState* tb_tournament_ = nullptr;
    int tb_pieces_                              = 0;

    // Games view.
    int games_filter_     = 0;
    int games_subject_    = -1;    // a tournament: participant index whose games are shown, -1 = all
    float eval_threshold_ = 2.0f;  // pawns, for the filters on what the evaluations tell
    std::vector<int> filtered_;
    std::size_t filtered_for_ = ~std::size_t(0);
    int selected_game_        = -1;
    std::optional<ReplayGame> replay_;
    std::string replay_error_;
    std::filesystem::path replay_log_;  // the engine log of the game shown, empty when it has none
    std::vector<std::pair<uint32_t, std::string>> replay_pv_;  // its PV warnings: (ply, warning)
    int replay_ply_   = 0;                                     // number of moves played on the replay board
    bool flip_replay_ = false;

    // Openings view: results by starting position.
    std::vector<tour::OpeningStats> by_opening_;
    std::size_t by_opening_for_ = ~std::size_t(0);  // games_.size() they were computed for
    int opening_show_           = 0;                // 0 all, 1 a colour won both games, 2 drawn twice, 3 decisive
    std::vector<int> opening_order_;                // rows shown, sorted
    bool opening_order_dirty_ = true;
    int selected_opening_     = -1;  // index into by_opening_

    // Books view: the library, and the book shown, read in the background.
    std::vector<BookEntry> books_;
    int selected_book_ = -1;
    struct BookReading {
        int index = -1;
        std::string path;
        std::future<std::shared_ptr<tour::BookScan>> result;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::shared_ptr<std::atomic<std::size_t>> done;
    };
    std::optional<BookReading> book_reading_;
    std::vector<std::future<std::shared_ptr<tour::BookScan>>> abandoned_scans_;  // cancelled, still ending
    std::shared_ptr<const tour::BookScan> book_scan_;                            // of book book_scan_index_
    int book_scan_index_ = -1;
    std::string book_scan_error_;
    int book_show_ = 0;  // 0 every opening, 1 those with a problem
    std::string book_search_;
    std::vector<int> book_rows_;  // openings shown
    bool book_rows_dirty_ = true;
    int book_item_        = -1;     // opening shown on the board
    bool drop_mirrored_   = false;  // the cleaned copy also leaves out mirrored positions

    // Engines view.
    std::string quick_test_engine_;    // the engine of the quick test running, empty when none
    std::string quick_test_opponent_;  // empty: the engine itself
    int selected_engine_ = -1;
    // engines.json is saved once typing in the detail panel pauses, not at every key.
    std::optional<std::chrono::steady_clock::time_point> engines_save_due_;
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
    Broadcaster broadcaster_;
    bool broadcast_editing_ = false;  // a folder, token or round field was being typed in at the last frame

    // Ratings page: every game of the profile, and what is computed from them.
    tour::GameDb game_db_;
    bool game_db_dirty_ = true;  // read the tournaments again
    bool ratings_dirty_ = true;  // compute again, with other filters
    tour::RatingList rating_list_;
    std::string rating_tc_;      // time control shown, empty = all
    std::string rating_anchor_;  // engine whose rating is fixed, empty = average 0
    double rating_anchor_elo_ = 0;
    std::string rating_group_;  // only the engines of this library group, empty = all
    int rating_period_    = 0;  // 0 every game, 1 the last 30 days, 2 the last 365 days
    int rating_min_games_ = 0;
    bool rating_external_ = true;  // with the games of the imported PGN files
    std::optional<int> ratings_tab_request_;
    std::string card_engine_;  // Engine tab
    tour::EngineRecord card_;
    std::vector<tour::RatingPoint> card_history_;
    struct SearchAverages {
        bool read      = false;
        uint64_t moves = 0;
        double depth = 0, nps = 0, time_ms = 0;
    } card_search_;
    std::string h2h_a_, h2h_b_;  // Head to head tab
    tour::HeadToHead h2h_;
    std::set<std::string> progress_engines_;  // Progress tab
    std::map<std::string, std::vector<tour::RatingPoint>> progress_;
    std::optional<uint64_t> select_game_id_;  // a game to show once its tournament is open

    // Shell.
    Page page_ = Page::Home;
    std::optional<Tab> tab_request_;  // tab to select on the next frame
    std::vector<tour::Summary> tournaments_;
    bool tournaments_dirty_  = true;
    std::size_t legacy_runs_ = 0;
    std::optional<ui::Theme> theme_applied_;
    int zoom_applied_ = 0;

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
