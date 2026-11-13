#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
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
#include "layout.hpp"
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
#include <tour/submission.hpp>
#include <tour/game_db.hpp>
#include <tour/incidents.hpp>
#include <tour/ratinglist.hpp>
#include <tour/standings.hpp>

struct SDL_Window;

namespace fcg {

// The date in the name of a tournament's folder: "20261109-185927_a_vs_b" -> "2026-11-09 18:59".
std::string folderDate(const std::filesystem::path& dir);

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

    // The window moved to a screen of another scale (or the screen's changed): fonts and sizes follow it.
    void onDisplayScale(float scale);

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
        // The game whose board was turned to the seed's side (gauntlet).
        std::size_t turned_for = ~std::size_t{0};
        std::size_t followed   = 0;  // moves shown, to keep the move list scrolled to the end
        int follow_frames      = 0;
    };

    enum class Page { Home, NewMatch, Tournament, Engines, Books, Suites, Ratings, Log, Settings };
    enum class Tab { Overview, Standings, Bracket, Schedule, Live, Games, Openings, Broadcast };

    // shell
    void drawShell();
    void drawRail(float width);
    [[nodiscard]] int gamesPlaying() const;  // games in progress, shown beside the live view in the navigation
    void drawHome();
    void drawNewMatch();
    // The setup of the new tournament page: its engines chosen, its SPRT bounds the wrong way round, an engine of it
    // being read. Start and Add to the queue follow them.
    [[nodiscard]] bool setupChosen() const;
    [[nodiscard]] bool setupBadSprt() const;
    [[nodiscard]] bool setupProbing() const;
    void drawWorkspace();
    void drawWorkspaceHeader();

    // The panels of the workspace (its views, docked in its dock space) and their layouts, see view_panels.cpp.
    [[nodiscard]] static ImGuiID workspaceDock();
    static void registerPanelSettings();  // what layout.ini keeps of them: which are open
    void loadPanels();                    // from layout.ini, once
    void drawWorkspaceDock();             // the dock space, under the header; the default layout if it has none
    void drawPanels();                    // every panel open, after the shell
    void drawPanelContent(const std::string& panel);
    void placeIfAsked(const std::string& panel);  // a panel floating alone in a layout built
    void drawLiveBadge();  // the number of games being played, on the tab (or the title) of the live view
    [[nodiscard]] bool panelAvailable(std::string_view panel) const;
    [[nodiscard]] bool panelOpen(std::string_view panel) const { return panels_open_.count(std::string(panel)) > 0; }
    void showPanel(const std::string& panel);  // opens it (docked again if it was closed) and brings it to the front
    void closePanel(const std::string& panel);
    void panelsChanged();  // panels_open_ changed: layout.ini saves it
    [[nodiscard]] std::vector<std::string> panelsAvailable() const;  // in the order of the menu and the palette
    void applyLayout(const layout::Layout& l);                       // on the next frame of the workspace
    bool applyLayout(const std::string& name);  // ready-made (by its key) or saved (by its name); false if none
    [[nodiscard]] layout::Layout currentLayout() const;
    bool saveLayoutAs(const std::string& name);  // trimmed; false without a name
    void deleteLayout(const std::string& name);
    void drawLayoutMenu();  // the Layout button of the tournament header
    void drawSaveLayout();  // Save the layout as…: its name

    void drawSettings();
    void drawRatings();
    void drawRatingFilters();
    void drawRatingList();
    void drawLibraryMatches();  // the players of the PGN files taken for library engines
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
    // The parts of the game followed, each also a panel of its own: shown beside the mosaic when their panel is
    // closed, in a detached board all together.
    enum LivePart : unsigned {
        LiveBoard    = 1,
        LivePlayers  = 2,
        LiveSearches = 4,
        LiveMoves    = 8,
        LiveChart    = 16,
        LiveAll      = 31
    };
    static constexpr std::array<std::pair<unsigned, const char*>, 5> LIVE_PARTS{
        {{LiveBoard, layout::panel::BOARD},
         {LivePlayers, layout::panel::PLAYERS},
         {LiveSearches, layout::panel::SEARCHES},
         {LiveMoves, layout::panel::MOVES},
         {LiveChart, layout::panel::CHART}}};
    void drawLiveGame(SlotView& view, const char* id, unsigned parts, int slot, bool allow_detach = true);
    void drawLiveBoard(SlotView& view, float size);
    void drawLiveBoardFitted(SlotView& view);
    void drawLivePlayer(SlotView& view, chess::Color color, bool search);
    void drawLiveGameLine(SlotView& view, int slot, bool allow_detach);
    void drawLiveMoves(SlotView& view, float height);
    void drawLiveChart(SlotView& view);
    void drawLiveSide(SlotView& view, unsigned parts, int slot, bool allow_detach);
    [[nodiscard]] unsigned livePartsHere() const;  // the parts whose panel is closed
    void drawLivePart(unsigned part);              // in its own panel
    void drawDetachedBoard(int slot);
    void drawMosaic(int cols, float card);
    void drawGames();
    void drawOpenings();
    void refreshOpenings();
    [[nodiscard]] std::vector<const tour::OpeningStats*> shownOpenings() const;  // filtered, in the shown order
    void drawResults();
    void drawCharts();
    void drawEngines();
    void drawConsole();
    void drawQuitModal();

    // The command palette (Ctrl+K) and the guide (F1, the `?` of the page titles), see view_palette.cpp.
    struct PaletteEntry {
        std::string label;
        std::string where;  // the page, the tournament or the menu it belongs to, shown beside it, searched after it
        std::function<void()> run;
    };
    void drawPalette();
    [[nodiscard]] std::vector<PaletteEntry> paletteEntries();
    [[nodiscard]] guide::Section guideSection() const;  // the section of the page and tab shown
    bool palette_request_ = false;                      // opens on the next frame (Ctrl+K, --focus palette)
    std::string palette_query_;
    int palette_selected_ = 0;  // in the matches, best first

    // tournaments
    [[nodiscard]] bool isMatch() const;
    void refreshTable();
    void drawTournamentOverview();
    void drawStandings();
    void drawStandingsTable(bool compact);
    void copyStandings(ui::CopyAs as);  // to the clipboard, in the order shown
    void drawStandingsRow(std::size_t i, bool compact, bool swiss);
    void drawPgnOutError();  // when the PGN file of setup "Also append every game to" cannot be written
    void drawCrosstable();
    void drawOpponents(int id);  // the games of a participant against each opponent, under the standings table
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
    void lichessTokenField();          // in the Broadcast tab and in Settings
    void saveLichessToken();           // to the profile, when it changed
    std::string lichess_token_;        // as typed
    std::string lichess_token_saved_;  // as the profile has it

    // setup sections
    void setupFormat();
    void setupParticipants();
    // "Add an engine…": the library's engines with their reference Elo, sorted and filtered by it, opponents
    // suggested around the first participant's. True when engines were added to `list`.
    bool participantPicker(std::vector<ParticipantSetup>& list);
    struct ParticipantPicker {
        std::string search;
        int elo_min = 0, elo_max = 0;  // reference Elo shown, 0: no bound
        bool works_only = false;
        std::set<std::string> ticked;
        int suggest = 8;      // opponents to suggest
        int range   = 150;    // Elo, around the first participant's
        std::string message;  // what the last suggestion found
    } picker_;
    int open_picker_ = 0;  // --focus setup-picker: 1 the picker opens, 2 with opponents suggested
    int selected_participant_ = -1;  // clicked in the list, for Ctrl+Up and Ctrl+Down
    void drawSetupSummary();
    void drawTemplates();
    void setupEngines();
    void setupEngineOptions();  // the settings forced on every engine
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
    void pollFingerprints();  // the CRCs refreshPrograms reads in the background
    // Reads the engine's options now, or first when another engine is being read.
    void startProbe(int index);
    void launchProbe(int index);  // reads it now: nothing else is being read
    // This engine's options are being read, or waiting to be.
    [[nodiscard]] bool probing(const std::string& name) const;
    void pollProbe();
    void applyProbe();  // the result of the read that ended
    // An engine of the setup: its read comes first.
    [[nodiscard]] bool inSetup(const std::string& name) const;
    // A start (of a run, a resume, the queue, a quick test) must not replace fastchess' configuration while a read or
    // a check reads it: it waits for the read of an engine it plays (`needs`), and interrupts the others, which are
    // done again later; it waits for a test suite to end (see startGate). Meanwhile no other read or check starts.
    // `key` names what it starts: asked again while it waits, nothing changes; asked while another start waits, it
    // is refused (see startAsked). `failed`, if any, is called instead of `start` when the read of an engine it needs
    // fails. True when `start` was put off (or refused).
    bool startAfterReads(const std::string& key, std::vector<std::string> needs, std::function<void()> start,
                         std::function<void()> failed = {});
    // Moves a start put off on: true once it may start.
    bool advanceStart();
    void pollStart();
    [[nodiscard]] static std::string resumeKey(const std::filesystem::path& dir) { return "resume:" + dir.string(); }
    // The start put off under `key`, if any.
    [[nodiscard]] bool startPending(const std::string& key) const {
        return pending_start_ && pending_start_->key == key;
    }
    // Instead of the button that started it, the start put off under `key`: what it waits for, and Cancel. False when
    // there is none.
    bool drawStartWaiting(const char* id, const std::string& key, bool icon = true);
    // What the card of the tournament put off under `key` says it waits for; empty when there is nothing to say.
    [[nodiscard]] std::string startWaitingText(const std::string& key) const;
    void startCheck(int index);
    void startQuickTest(int index);
    // The quick test of the engine named so, put off: started once the engine is read, under its name then.
    std::function<void()> quickTestLater(const std::string& name);
    void finishQuickTest();
    void pollCheck();
    // Shows what an engine list or a folder holds, to choose the engines to add. `refreshed`: the remembered folder
    // looked in again, whose engines already in the library are left out. `list`: the engine list read, if it is one.
    void importEngines(const ImportResult& imported, const std::filesystem::path& refreshed = {},
                       const std::filesystem::path& list = {});
    void refreshEngineFolder();  // the new engines of the remembered folder, see Storage::engineFolder
    [[nodiscard]] const std::filesystem::path& engineFolder();
    void setInactive(const std::vector<std::string>& names, bool inactive);
    void drawImportPreview();
    void addImported();  // the engines chosen
    // A rating list (a list site's text or page, Ordo, CSV) whose ratings become the reference Elo of the library engines it names, once
    // seen in a preview.
    void openRatingImport(const std::string& text = {});
    void loadRatingList(const std::string& path);  // a file chosen, read into the preview
    void readRatingImport();                       // the text of the preview, read and matched again
    void drawRatingImport();
    void applyRatingImport();
    [[nodiscard]] bool engineInUse(const std::string& name) const;
    void addEngine(const std::string& path);
    std::string uniqueEngineName(const std::string& base, int ignore_index) const;
    // Renames the engine (made unique), and what refers to it by name follows: the setup and the queued tournaments,
    // the read and check queues, the quick test, the relaunch, the schedule's edit and the test suite.
    void renameEngine(int index, const std::string& name);
    void removeEngines(const std::vector<std::string>& names);
    void queueChecks(const std::vector<std::string>& names);          // checked one after the other, see pollCheck
    [[nodiscard]] std::vector<std::string> uncheckedEngines() const;  // what "Check those not checked" checks
    void askEngineList();                                             // Import > An engine list…: the file dialog

    // run lifecycle
    void startRun();
    void resumeRun(bool at_launch = false);
    void resumeAtLaunch();           // the tournament running when the program ended, see RunningMarker
    void setRunPaused(bool paused);  // and the marker follows
    void stopRun(bool now);          // the user's stop: now, or once the running games are over
    [[nodiscard]] std::string runMarkerDir() const;  // the running tournament's folder, as the marker names it
    void enqueue();         // the current setup, to start after the running tournament
    // The first queued tournament, if any; with `go_on`, the next ones when it cannot start. One that cannot
    // start stays queued. `from`: the tournament it goes on from, named `label` (put off while an engine of it was
    // read, or the next after one that could not start); when the queue changed meanwhile, from the first.
    void startFromQueue(bool go_on = false, std::size_t from = 0, const std::string& label = {});
    void drawQueue();
    void drawHealth();  // the home page's: incidents of the last 24 hours, memory, engine processes
    // About how long the running tournament has left at its pace; nullopt without a game count or a pace yet.
    [[nodiscard]] std::optional<double> runSecondsLeft() const;
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
    // Export for a rating list: the dialog, then the files once their place is chosen (view_submission.cpp).
    void openSubmissionExport();
    void drawSubmissionExport();
    void writeSubmissionExport(const std::string& path);
    void drawSubmissionChecklist();  // in the overview of a finished tournament

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
    bool live_mosaic_       = true;
    MoveChart live_chart_   = MoveChart::Eval;
    MoveChart replay_chart_ = MoveChart::Eval;

    // Schedule tab: its rows and what is shown with them, computed again when the tournament changes (its
    // version), not every frame.
    struct ScheduleGroup {
        std::size_t first = 0, last = 0;  // rows
        uint32_t number  = 0;             // cycle, or round
        int block        = 0;             // a cycle can come in two blocks
        std::size_t done = 0, total = 0;
        bool running = false;
    };
    struct ScheduleView {
        const tour::TournamentState* tournament = nullptr;
        uint64_t version                        = ~uint64_t(0);
        std::vector<tour::ScheduleRow> rows;
        std::vector<ScheduleGroup> groups;
        std::map<uint64_t, std::string> result_of;  // by game number
        std::map<uint32_t, std::string> openings;   // the names of the openings encounters started with
        std::map<std::pair<uint32_t, uint32_t>, std::string> round_openings;  // (cycle, round), one opening per round
        std::size_t done = 0, running = 0, pending = 0;
        uint32_t last_round = 0;
    } schedule_;
    void refreshSchedule();

    // SPRT calculator: its own copy of the bounds, applied to the setup on request.

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
    int replay_followed_ = -1;                                    // replay_ply_ the move list was last scrolled to
    bool flip_replay_ = false;

    // Openings view: results by starting position.
    std::vector<tour::OpeningStats> by_opening_;
    std::size_t by_opening_for_ = ~std::size_t(0);  // games_.size() they were computed for
    int opening_show_           = 0;                // 0 all, 1 a colour won both games, 2 drawn twice, 3 decisive
    std::vector<int> opening_order_;                // rows shown, sorted
    bool opening_order_dirty_ = true;
    int selected_opening_     = -1;  // index into by_opening_
    std::vector<int> opening_games_;  // indexes into games_ of the selected opening's games
    int opening_games_for_ = -1;      // the opening they are of, -1 when to find again

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
    EngineEntry probe_entry_;   // the engine being read, as it was then: changed since, its result is stale
    bool probe_again_ = false;  // asked for again while it was read: its result is dropped, it is read again
    // A start put off, see startAfterReads.
    struct PendingStart : StartRequest {
        std::function<void()> start;
        std::function<void()> failed;  // instead, when the read of an engine it needs fails
        std::string waiting_for;       // the engine whose read it waits for; empty while reads are interrupted
        bool waiting_check = false;    // ... or whose check
        bool waiting_suite = false;    // ... or the test suite
    };
    std::optional<PendingStart> pending_start_;
    std::function<void()> start_failed_;  // the `failed` of a start whose read failed, called at the next frame
    // Set to interrupt the read or the check in flight (its engine process ended too): it is done again later.
    std::shared_ptr<std::atomic_bool> probe_interrupted_, check_interrupted_;
    std::vector<std::string> probe_queue_;  // engine names waiting for a probe
    std::string probe_message_;
    std::string option_filter_;
    std::future<CheckResult> check_;
    int check_index_ = -1;
    std::vector<std::string> check_queue_;  // engine names waiting for a check
    std::atomic<bool> fingerprints_cancel_{false};
    std::future<std::vector<std::pair<std::string, tour::FileFingerprint>>> fingerprints_;  // by program
    bool refresh_programs_again_ = false;  // asked while the CRCs were read
    std::string engine_search_;
    std::string engine_group_;  // shown group, empty = all
    int engine_status_ = 0;     // shown: 0 all, 1 works, 2 failed or changed, 3 not checked, 4 missing, 5 WinBoard
    std::set<std::string> engine_marked_;    // several engines chosen (Ctrl or Shift click), by name
    std::vector<std::string> remove_names_;  // waiting for the removal to be confirmed
    std::string group_to_add_;
    std::string option_browse_;  // the UCI option a file dialog is open for
    struct ImportPreview {
        ImportResult result;
        std::string refreshed;     // the remembered folder, looked in again: only its new engines are shown
        std::string list;          // the engine list read, empty for a folder
        std::vector<char> known;   // by engine: already in the list, not added again
        std::vector<char> chosen;  // by engine: to add
    };
    std::optional<ImportPreview> import_preview_;
    std::optional<std::filesystem::path> engine_folder_;  // settings.json's, read once
    struct RatingImport {
        std::string text;  // as pasted or read from a file
        struct Row {
            std::string engine, listed;  // the library's name and the list's
            int elo_now = 0, elo = 0;
            bool chosen = true;
        };
        std::vector<Row> rows;               // the library engines the list names
        std::vector<std::string> unmatched;  // the names of the list that are no library engine
        std::size_t listed = 0;              // engines on the list
    };
    std::optional<RatingImport> rating_import_;
    bool open_rating_import_  = false;
    bool open_import_preview_ = false;

    // Console view.
    std::vector<Console::Line> console_lines_;
    uint64_t console_version_ = 0;
    bool console_follow_      = true;

    // Standings of a round robin or gauntlet, recomputed when games arrive.
    tour::Table table_;
    tour::Table shown_;  // table_ in the order of the Standings tab, which its copies and export follow
    tour::Order standings_order_ = tour::Order::Points;
    std::size_t table_for_       = ~std::size_t(0);
    std::chrono::steady_clock::time_point table_next_{};  // a big table waits a while before it is computed again
    std::optional<tour::Anchor> anchor_;
    int crosstable_mode_ = 0;                             // 0 score, 1 wins/draws/losses, 2 Elo difference
    int standings_view_  = -1;                            // 0 table, 1 crosstable, -1 as suits the tournament
    std::optional<std::pair<int, int>> crosstable_cell_;  // participant ids (row, column)
    std::optional<int> standings_engine_;  // participant id whose opponents are listed under the table
    struct SubmissionDialog {  // Export for a rating list, from the Games tab
        bool open = false;
        bool zip  = true;
        tour::SubmissionExport e;
    } submission_;
    tour::SubmissionFacts submission_facts_;  // of the checklist, for the tournament at this version()
    uint64_t submission_facts_for_ = ~uint64_t(0);
    int submission_tb_pieces_      = 0;  // the pieces of the first engine's SyzygyPath, read with submission_facts_
    std::string template_name_;
    std::vector<std::string> template_names_;  // the templates folder, listed now and then rather than every frame
    std::chrono::steady_clock::time_point templates_listed_{};
    std::string template_message_;  // why the last save or load failed
    std::string template_replace_;  // the file name of a template to replace, once confirmed

    // Schedule tab and edits.
    int edit_kind_ = 0;        // tour::Edit::Kind
    std::string edit_engine_;  // engine list entry to add or bring in
    std::string name_draft_of_;  // engine whose Name field is being edited, and the name typed so far
    std::string name_draft_;
    bool edit_catch_up_     = true;
    int edit_participant_   = -1;  // participant id
    bool edit_void_played_  = false;
    int edit_cycles_        = 0;
    int edit_replay_filter_ = 0;  // 0 losses on time and crashes, 1 every game of a participant
    std::string edit_message_;
    bool edit_error_ = false;
    // The games "Play games again" picks and the impact of the edit, computed again when they may have changed.
    struct EditPreview {
        std::string games_key;  // tournament version, filter, participant
        std::vector<std::pair<uint32_t, int>> games;
        std::string key;  // tournament version and the whole edit
        tour::Impact impact;
    } edit_preview_;

    // Queue and relaunch.
    std::vector<QueuedTournament> queue_;
    std::string relaunch_engine_;

    // Broadcast.
    Broadcaster broadcaster_;
    bool broadcast_editing_ = false;  // a folder, token or round field was being typed in at the last frame

    // Ratings page: every game of the profile, and what is computed from them, on a worker thread: a rating list
    // of thousands of engines takes a moment.
    struct RatingsResult {
        std::shared_ptr<const tour::GameDb> db;
        std::string anchor;  // the one used: the default when none was chosen
        double anchor_elo = 0;
        tour::RatingList list;
        std::string card_engine;
        tour::EngineRecord card;
        std::vector<tour::RatingPoint> card_history;
        std::string h2h_a, h2h_b;
        tour::HeadToHead h2h;
        std::set<std::string> progress_engines;
        std::map<std::string, std::vector<tour::RatingPoint>> progress;
    };
    std::future<RatingsResult> ratings_job_;
    std::shared_ptr<const tour::GameDb> game_db_ = std::make_shared<tour::GameDb>();
    bool game_db_dirty_ = true;  // read the tournaments again
    std::vector<std::string> game_db_library_;  // the library's names the PGN files' players were matched to
    bool ratings_dirty_ = true;  // compute again, with other filters
    std::optional<std::vector<std::filesystem::path>> rating_pgns_;  // settings.json's, read once
    const std::vector<std::filesystem::path>& ratingPgns();
    void setRatingPgns(const std::vector<std::filesystem::path>& files);
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
    Tab shown_tab_ = Tab::Overview;   // the tab of the panel focused last, for the guide
    // The panels of the workspace (view_panels.cpp).
    std::set<std::string> panels_open_;
    bool panels_loaded_  = false;
    bool layout_checked_ = false;                   // the dock space was found usable, or got the default layout
    std::optional<layout::Layout> layout_request_;  // built on the next frame of the workspace
    std::set<std::string> redock_;                  // shown again after being closed: docked in the workspace
    std::optional<std::string> panel_request_;      // focused and brought to the front once shown
    int panel_request_frames_ = 0;                  // the frames it was shown without being in front
    std::set<std::string> fronts_;                  // to bring to the front of their dock node (a layout built)
    std::optional<layout::Layout> built_;           // the layout built last, its tabs put in order for a few frames
    int built_frames_ = 0;                          // the frames of the workspace since
    std::vector<layout::Placement> placements_;     // panels floating alone, placed at their next frame
    std::string panel_focused_;                     // the panel last focused, for the guide
    std::vector<layout::Layout> layouts_;           // the user's own, kept in layouts.json
    bool open_save_layout_ = false;
    bool open_layout_menu_ = false;  // --focus layout-menu
    std::string layout_name_;        // being typed in Save the layout as…

    std::vector<tour::Summary> tournaments_;
    bool tournaments_dirty_  = true;
    std::size_t legacy_runs_ = 0;
    // The list read on a worker thread, each journal again only when it changed.
    struct TournamentList {
        std::vector<tour::Summary> summaries;
        std::size_t legacy_runs = 0;
    };
    std::future<TournamentList> tournaments_job_;
    std::shared_ptr<tour::SummaryCache> summary_cache_ = std::make_shared<tour::SummaryCache>();
    bool tournaments_read_                             = false;  // once at least
    // The health panel's incidents, read on a worker thread now and then.
    std::shared_ptr<tour::IncidentScanner> incident_scanner_ = std::make_shared<tour::IncidentScanner>();
    std::future<tour::Incidents> incidents_job_;
    std::optional<tour::Incidents> incidents_;
    std::chrono::steady_clock::time_point incidents_asked_{};
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
    bool launch_checked_  = false;  // resumeAtLaunch() done
    bool user_stop_       = false;  // the user stopped the running tournament (not by quitting)
    bool resume_guarded_  = false;  // resumed at launch, the marker says so until it has played a while
    std::filesystem::path resume_crashed_;  // the tournament whose resume at launch ended the program
};

}  // namespace fcg
