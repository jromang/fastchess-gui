#pragma once

#include <array>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <deque>
#include <filesystem>
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

#include "board_3d.hpp"
#include "board_nav.hpp"
#include "board_page.hpp"
#include "board_view.hpp"
#include "book_jobs.hpp"
#include "broadcaster.hpp"
#include "console.hpp"
#include "dialogs.hpp"
#include "engine_changes.hpp"
#include "engine_import.hpp"
#include "engines.hpp"
#include "game_graph.hpp"
#include "guide_view.hpp"
#include "home.hpp"
#include "layout.hpp"
#include "live.hpp"
#include "machine_jobs.hpp"
#include "machine_run.hpp"
#include "model.hpp"
#include "notice.hpp"
#include "pgn_store.hpp"
#include "portable.hpp"
#include "profiles.hpp"
#include "qr.hpp"
#include "remote_run.hpp"
#include "run.hpp"
#include "stats.hpp"
#include "storage.hpp"
#include "suite_run.hpp"
#include "tablebases.hpp"
#include "topology.hpp"
#include "ui.hpp"

#include <tour/book_scan.hpp>
#include <tour/by_opening.hpp>
#include <tour/game_db.hpp>
#include <tour/incidents.hpp>
#include <tour/ratinglist.hpp>
#include <tour/standings.hpp>
#include <tour/submission.hpp>
#include <tour/teams.hpp>

struct SDL_Window;

namespace fcg {

class EngineHub;

// The Games tab's filter on the games whose opening was flagged unbalanced (the last of its list).
inline constexpr int UNBALANCED_FILTER = 11;
// The Openings tab's view of the openings with such a game.
inline constexpr int UNBALANCED_OPENINGS = 4;

// The date in the name of a tournament's folder: "20261109-185927_a_vs_b" -> "2026-11-09 18:59".
std::string folderDate(const std::filesystem::path& dir);

// The PgnField a chart reads from the move comments of a PGN: 0 for the evaluation, the time and the depth, which
// are always written.
constexpr uint8_t chartField(GraphChart chart) {
    switch (chart) {
        case GraphChart::Nodes:
            return PGN_NODES;
        case GraphChart::Speed:
            return PGN_NPS;
        case GraphChart::TbHits:
            return PGN_TBHITS;
        case GraphChart::Predicted:
            return PGN_PV;
        default:
            return 0;
    }
}
// The PgnField of its thinner line: the decision time beside the move time, the seldepth beside the depth.
constexpr uint8_t chartExtraField(GraphChart chart) {
    return chart == GraphChart::Time ? PGN_DECISION : chart == GraphChart::Depth ? PGN_SELDEPTH : 0;
}
// The name of the setup's PGN option that writes a PgnField.
const char* pgnFieldOption(uint8_t field);
// Whether a tournament's PGN options write a PgnField.
bool pgnFieldOn(const fastchess::config::Pgn& pgn, uint8_t field);
// Why a game's PGN lacks a PgnField: the option that writes it was off (`on` false), or it was on and the engines
// did not give what it needs (the decision time is only written when their info lines tell it); nullopt when the
// tournament's options are not known.
std::string missingPgnFieldTip(uint8_t field, std::optional<bool> on);

// "W 12 · D 80 · L 8 %" and a thin bar of the three, for a side's own win, draw, loss in permille.
void wdlLine(const std::array<int64_t, 3>& wdl);

// After a PGN file was added for the ratings: whether to show its players, for the user to say which library engine
// they are. Those that library engines may be (two of the same name, or one by a hint), that the user has not decided
// (an engine chosen, or a separate one) and that were not offered before the file was added: `before`, their names
// folded (tour::foldedName()).
[[nodiscard]] bool reviewPgnPlayers(const std::vector<tour::GameDb::Player>& players,
                                    const std::set<std::string>& before, const std::vector<std::string>& separate);

// The move the side to move in a live game thinks of and the reply it expects (the first two moves of its PV), each
// in the colour of the engine that would play it, the reply lighter; none between searches. `board`: the position.
[[nodiscard]] std::vector<BoardArrow> pvArrows(const SlotState& st, const chess::Board& board);

struct AppOptions {
    bool autostart = false;
    std::string focus_window;  // page or tab shown at startup (for screenshots)
    float ui_scale = 1.0f;     // DPI scale, to rebuild the style when the theme changes
    bool scripted  = false;    // scripted screenshots: nothing is asked from the internet
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
    // Frames wanted at a steadier pace than the usual 30 a second: a 3D board's camera moving.
    [[nodiscard]] bool wantsSmoothFrames() const { return board3d_.moving(); }

    // Quits without asking (--shot, --quit-after): what this window started stops, the tournaments found playing at
    // launch play on.
    void forceQuit();
    // Quits, the tournaments playing on in their own processes (the quick test stops).
    void keepRunningAndQuit();

    // The window came back to the front: an engine may have been rebuilt or moved meanwhile.
    void onFocusGained() { refreshPrograms(); }

    // A file dropped on the window: a PGN file, or a FEN or EPD position, opens on the Board page.
    void dropFile(const std::string& path);

    // The window moved to a screen of another scale (or the screen's changed): fonts and sizes follow it.
    void onDisplayScale(float scale);

    // For scripted screenshots.
    void focusWindow(const std::string& name);
    bool debugSelectGame(int index);

    [[nodiscard]] bool initGraphics();

   private:
    struct OpenRun {
        std::unique_ptr<Run> run;
        double weight   = 1;  // its share of the machine against the other tournaments' (budget.hpp)
        bool quick_test = false;
        // Found playing at launch (it played before this window): forceQuit() lets it play on.
        bool found = false;
        std::string finish_message;
        std::vector<Fix> finish_fixes;
        std::string finish_fixes_for, stopped_by;
        bool user_stop = false, resume_guarded = false;
        std::size_t games_at_start = 0;
        uint64_t console_mark      = 0;
        // watchStartFailures(): the version of its StartFailures seen, and the participants told set aside.
        uint64_t start_failures_seen = ~uint64_t(0);
        std::set<int> start_failures_told;
        // Played on a remote machine (machine_run.hpp): which, what became of its process there, and its files
        // copied back. Empty and null for a tournament of this computer.
        std::string machine_id, machine_name;
        std::shared_ptr<LinkStatus> link_status;
        std::shared_ptr<MachineSync> sync;
        bool sync_finishing = false;
    };
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
        bool last_capture      = false;  // the last move took a piece: its sound (hearMoves)
        // The position the board shows: the last, unless the user stepped back in the game (reset for each game).
        FollowedPly nav;
        std::size_t nav_game = ~std::size_t{0};
        int listed_ply       = -1;  // nav's ply the move list was last scrolled to
        Clock::time_point last_active{};  // the last frame its game was being played (shownSlots)
        // Its opening and the end of its book, read again when a move comes (liveOpening): for the game and the
        // number of moves it was read at.
        LiveOpening opening;
        std::pair<std::size_t, std::size_t> opening_at{~std::size_t{0}, ~std::size_t{0}};
        float cards_fixed = 0;  // what the two engine cards took besides their lines, the last frame
        // What each side's card, or its search lines in a narrow panel, has needed so far in this game (reset for
        // each game): they keep that height, so that the moves and the curve under them stay where they are (#193).
        std::array<int, 2> pv_rows{};
        std::array<float, 2> card_h{};
        std::array<float, 2> search_h{};
    };

    enum class Page { Home, NewMatch, Tournament, Engines, Books, Board, Suites, Ratings, Log, Settings };
    enum class Tab { Overview, Standings, Bracket, Schedule, Live, Games, Openings, Broadcast };

    // shell
    void drawShell();
    void drawRail(float width);
    // Games in progress: of the tournament shown (beside the live view), or of every tournament (the rail).
    [[nodiscard]] int gamesPlaying(bool all = false) const;

    // The home page as a control room (view_home.cpp): Pause all, the cores in use, what needs action first, the card
    // of the running tournament, the queue and when it all ends, the last tournaments finished with their result,
    // the health of the last 24 hours, then every tournament.
    void drawHome();
    void drawHomeHeader();      // the title, Pause all (or Resume all), Open a folder… and New tournament
    void drawGettingStarted();  // the steps of the guide's Quick start, until a first tournament is started
    // The machine's resources and who uses them: the cores of the running games against the hardware threads. The
    // memory and the GPUs join them once tournaments have a budget of them.
    [[nodiscard]] std::vector<home::ResourceBar> resourceBars();
    // The cores of the games being played: for each, the Threads of its engine that has the most (the two engines
    // think in turn).
    [[nodiscard]] int coresInUse();
    // The card of the running tournament, made from the run: one per tournament once several run at once.
    [[nodiscard]] home::RunCard runCard();
    void drawRunCards();
    // An engine that cannot start, the tournament an error stopped (with the fixes of its message, and Resume), a
    // drive too full: each with its fix. True when anything was shown.
    bool drawNeedsAction();
    // The last run ended by an error or an engine that stopped it, and the card saying so was not hidden.
    [[nodiscard]] bool stoppedByError() const;
    // The card of the tournament that can be resumed, with the error that stopped it when `error`.
    void drawInterrupted(bool error);
    void drawLastFinished();    // the last tournaments finished, their result, Relaunch with… and Open
    void drawAllTournaments();  // the table of every tournament, folded
    void pollDiskSpace();       // the free space of the tournaments' drives, asked on a worker thread now and then
    // Pause all: the running tournament paused and the queue held (it starts no tournament until Resume all).
    [[nodiscard]] bool canPauseAll() const;
    [[nodiscard]] bool allPaused() const;
    void pauseAll();
    void resumeAll();
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
    void drawPgnPlayers();      // the window where the user says which library engine a PGN player is
    void drawEngineCard();
    void drawHeadToHead();
    void drawProgress();
    void refreshRatings();
    [[nodiscard]] tour::GameFilter ratingFilter() const;
    [[nodiscard]] tour::RatingListInfo ratingInfo() const;  // rating_info_ with today's date and the version
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
    // The keys through the moves (navKeys) for the game of `view`, the window drawn having the focus.
    void liveNavKeys(SlotView& view);
    // The room the buttons take under a live board: none when the setting hides them.
    [[nodiscard]] float liveNavHeight() const;
    // The height its line took (in a card), `pv_room` at most.
    float drawLivePlayer(SlotView& view, chess::Color color, bool search, float pv_room = FLT_MAX);
    // The same in a large panel: a card with the logo as high as the name and the clock, and its search figure by
    // figure with the whole line, as much of it as `pv_room` holds.
    float drawLivePlayerCard(SlotView& view, chess::Color color, bool search, float width, float pv_room);
    void drawLiveGameLine(SlotView& view, int slot, bool allow_detach);
    // Above the moves: the game's opening, with where its book ended, and the tables' verdict once they know the
    // position shown.
    void drawLiveFacts(SlotView& view);
    void drawLiveMoves(SlotView& view, float height);
    // The score of a move or a search, from its mover's side, as the Live tab shows it: the mover's, or White's.
    [[nodiscard]] std::optional<fastchess::Score> liveScore(std::optional<fastchess::Score> score,
                                                            chess::Color mover) const;
    void drawLiveChart(SlotView& view);
    void drawLiveSide(SlotView& view, unsigned parts, int slot, bool allow_detach);
    [[nodiscard]] unsigned livePartsHere() const;  // the parts whose panel is closed
    void drawLivePart(unsigned part);              // in its own panel
    void drawDetachedBoard(int slot);
    void drawMosaic(int cols, float card);

    // Big screen (view_big_screen.cpp): one game filling the main window, or a detached board's window.
    struct BigView {
        int slot  = 0;                                   // the game shown
        int panel = -1;                                  // a detached board's: its slot; -1 in the main window
        std::chrono::steady_clock::time_point turned{};  // when it last turned to a game, to follow them
        bool controls_hovered = false;
    };
    void startBigScreen(int slot);  // in the main window
    void leaveBigScreen();
    // A detached board shown as a big screen in its own window, or, docked in the main one, the main window's.
    void startBigBoard(int slot);
    void leaveBigBoard(int slot);
    void toggleFullscreen(SDL_Window* window);
    void handleBigScreenKeys();  // F11, Esc, the arrows, + and -; the mouse moving shows the controls
    void drawBigScreen();        // the main window's, instead of the pages
    void drawBigGame(BigView& big, bool main_window);
    void drawBigControls(BigView& big, bool main_window, ImVec2 origin, int slots);
    void drawGames();
    void drawOpenings();
    void refreshOpenings();
    [[nodiscard]] std::vector<const tour::OpeningStats*> shownOpenings() const;  // filtered, in the shown order
    void drawResults();
    void drawCharts();
    // The search of the participants given by their indexes, a row each, by default in that order (a click on a column
    // sorts by it): in the overview of a match and, in the order of the standings, of a tournament.
    void drawSearchTable(const std::vector<std::size_t>& participants);
    void drawEngines();
    void drawConsole();
    // The engine console (view_engine_console.cpp), in the card of its engine.
    void openEngineConsole(int index);
    void closeEngineConsole();
    void pollEngineConsole();  // every frame, shown or not
    void drawEngineConsole(const EngineEntry& e);
    void saveConsoleLog(const std::string& path);

    // The Board page (view_board.cpp): a game or a position, and its analysis.
    void drawBoardPage();
    void drawBoardColumn(float size);  // the board, the buttons under it
    void drawBoardAnalysis();
    void drawBoardMoves(float height);
    void drawBoardTable();   // the Table tab: each move with its engine's score, depth and time
    void drawBoardReview();  // the Review tab: each side's accuracy and figures, the key moments, who saw them
    [[nodiscard]] std::string boardMoveLabel(int ply) const;  // "12…Qa5", the game's move leading to `ply`
    // The line shown on the board: the game (-1) or a variation, at its position `ply`.
    void showBoardLine(int line, int ply);
    // The position of the game the board's position is on, or leaves it at (a variation).
    [[nodiscard]] int boardMainPly() const;
    // The players above and below the board: name, evaluation of its last move, time, clock at the move shown.
    [[nodiscard]] bool boardStrips() const;  // the game has players to show
    void updateBoardTablebase();             // the tablebases' result of each position of the board's game
    [[nodiscard]] int boardTablebasePly() const;  // the first position they know, -1 none
    void drawPlayerStrip(chess::Color side, float size, const std::vector<std::optional<int64_t>>& clocks);
    void drawBoardGraph(float height);  // the game graph card, under the moves
    [[nodiscard]] float boardGraphHeight() const;
    [[nodiscard]] std::string boardGraphTip(int ply) const;  // the graph's tooltip at a position
    void pollBoard();  // every frame: the analysis follows the board while the page is shown
    void loadBoardSettings();
    void saveBoardSettings();
    // Opens `game` on the Board page after `ply` moves, the page and tab shown kept to go back to; `analyse` starts
    // the analysis. `title` says what the game is.
    void openOnBoard(ReplayGame game, int ply, bool chess960, std::string title, bool analyse);
    void openPositionOnBoard(const std::string& fen, bool chess960, std::string title, bool analyse);
    void setBoardAnalysis(bool on);
    void setBoardThreat(bool on);  // X: the opponent's best move were it to move again
    // Ends the analysis engine and the threat engine, the analysis and the threat off: stopped, an engine stays
    // started, its hash table (a GPU's memory) held, for the next search.
    void unloadBoardEngines();
    [[nodiscard]] std::optional<AnalysisPosition> threatPosition() const;  // the board's, the turn passed
    // Key A, pressed for a board now: once a frame, not while a text field or a popup has the keys.
    [[nodiscard]] bool boardKey();
    [[nodiscard]] std::string boardFen();  // the position of the Board page
    void boardPlay(chess::Move move);
    // A move made on the board with the mouse: the user's move in the game they play, else boardPlay().
    void boardMove(chess::Move move);
    void boardClick(const chess::Board& board, chess::Square square);
    // The left button on the board (board_.drag): `press`, the square it went down on this frame; `hover`, the square
    // under the pointer; `right`, a right click. A press acts as a click (boardClick); a piece picked up and dragged
    // lands where it is released when it may go there, and else stays picked up; one pressed again without moving is
    // put down.
    void boardPointer(const chess::Board& board, std::optional<chess::Square> press, std::optional<chess::Square> hover,
                      bool right);
    // The threads the running tournament's games use now, about: its games times the larger thread count of its
    // engines.
    [[nodiscard]] int tournamentThreads() const;
    // A, on the boards of the other pages: their position on the Board page, analysed.
    void analyseLiveGame(int slot);
    void analyseReplay();
    // Games and positions from elsewhere: a PGN file read in the background, the clipboard, a text.
    void openBoardPgn(const std::filesystem::path& file);
    void pollBoardPgn();
    void showPgnSource(PgnSource source);  // its first game on the board, its games listed
    void loadPgnGame(int index);
    void readOnBoard(const std::string& text, const std::string& name);  // a FEN, a PGN, moves
    void pasteOnBoard();
    void drawPgnGames();
    void openPositionEditor();
    void drawPositionEditor(float size);
    // A game analysed move by move, in the background, by the analysis engine (one line).
    [[nodiscard]] const EngineEntry* analysisEngine();  // the one chosen, or the first that works; null without any
    void startGameReview();
    void cancelGameReview();
    void pollGameReview();
    void drawGameReview();  // what it found
    // The analysis shown with the board's game: the one running for it (its latest values), else the first kept.
    [[nodiscard]] const GameReview* shownReview() const;
    // Every analysis of the board's game, the judging one first: those running with their latest values.
    [[nodiscard]] std::vector<const GameReview*> shownReviews() const;
    // Another engine of the library analyses the board's game too; an analysis judges or goes.
    void addReviewEngine(const std::string& engine);
    void judgeWith(const std::string& engine);
    void removeReview(const std::string& engine);
    void startReviewTask(const EngineEntry& engine, const std::vector<GameReview>& kept);
    [[nodiscard]] std::string judgementTip(int ply, const MoveReview& judged) const;
    // The position after the next (1) or previous (-1) judged move from the board's; -1 when there is none.
    [[nodiscard]] int judgedPly(int direction) const;
    void drawThisMove();  // under the moves: what the engine that played the move said, and each analysis's verdict
    [[nodiscard]] float thisMoveHeight() const;
    // Games between two engines of the library from the board's position, played apart from any tournament.
    void startBoardGames();
    void pollBoardGames();  // every frame: the board follows the game being played
    void drawBoardPlay();   // their settings, then the games as they are played
    [[nodiscard]] int engineThreads(const std::string& name) const;  // a library engine's Threads, 1 without
    void drawQuitModal();

    // The command palette (Ctrl+K) and the guide (F1, the `?` of the page titles), see view_palette.cpp.
    struct PaletteEntry {
        std::string label;
        std::string where;  // the page, the tournament or the menu it belongs to, shown beside it, searched after it
        std::function<void()> run;
    };
    void drawPalette();
    [[nodiscard]] std::vector<PaletteEntry> paletteEntries();
    [[nodiscard]] const char* guideSection() const;  // the guide's topic of the page and tab shown
    void toggleGuide();                              // F1
    bool palette_request_ = false;                   // opens on the next frame (Ctrl+K, --focus palette)
    std::string palette_query_;
    int palette_selected_ = 0;  // in the matches, best first
    // The guide panel at the right of the page (guide_view.hpp), and the home page's Getting started card hidden.
    GuideView guide_;
    bool getting_started_hidden_ = false;

    // tournaments
    [[nodiscard]] bool isMatch() const;
    void refreshTable();
    void drawTournamentOverview();
    void drawStandings();
    void drawStandingsTable(bool compact);
    void copyStandings(ui::CopyAs as);  // to the clipboard, in the order shown
    void drawStandingsRow(std::size_t i, bool compact, bool swiss);
    void drawPgnOutError();  // when the PGN file of setup "Also append every game to" cannot be written
    // The games from unbalanced openings and the openings replaced, when the tournament looks for them.
    void drawUnbalancedCount();
    void showUnbalancedGames();  // the Games tab, filtered on them
    void drawCrosstable();
    void drawOpponents(int id);  // the games of a participant against each opponent, under the standings table
    void drawTeamScore();        // a team match: the teams' score, its Elo and LOS
    void drawTeamStandings();    // a team match: the score, then each team's engines against the other team's
    void drawSchedule();
    void drawBracket();
    void drawEditPanel();
    void applyTournamentEdit(const tour::Edit& edit);
    void undoTournamentEdit();
    // The Schedule tab's change panel, set to withdraw or replace `participant`: its impact shows before anything
    // changes.
    void openParticipantEdit(tour::Edit::Kind kind, int participant);
    void refreshAfterEdit();
    // The context menu of a finished game (Games and Schedule tabs): play it again, void it, set its result.
    void drawGameMenu(uint32_t encounter, int game);
    // The context menu of a game being played (Live tab, detached boards): end it with a result.
    void drawEndGameMenu(int slot);
    // The result that counts of game `number` (its number()), nullptr when none does; asked of the tournament once
    // per change of it.
    [[nodiscard]] const tour::GameResult* gameResult(uint64_t number);

    // broadcast
    void updateBroadcast();
    void drawBroadcast();
    void drawLichess(bool& changed);
    void drawWebChat(bool& changed);   // under the web page: the chat link and the Discord widget
    void drawTunnel(bool& changed);    // under the web page
    void lichessTokenField();          // in the Broadcast tab and in Settings
    void saveLichessToken();           // to the profile, when it changed
    std::string lichess_token_;        // as typed
    std::string lichess_token_saved_;  // as the profile has it

    // setup sections
    void setupFormat();
    bool setupTeamOptions();  // a team match: the pairings and the teams' names; true when they changed
    // The participants, seeds first, each on a line wide enough for its name, with its reference Elo, what it plays
    // differently from the common settings, and ⋯ for its own settings. Ctrl and Shift select several; a drag moves
    // them; Delete removes them, Ctrl+Up and Ctrl+Down send them to the top or the bottom, Alt+Up and Alt+Down move
    // them by one.
    void setupParticipants();
    ImGuiSelectionBasicStorage participants_selected_;  // by index
    bool participants_focused_ = false;  // a participant had the keyboard at the last frame: the keys above apply
    int participant_menu_      = -1;     // the participant whose ⋯ menu opens on the next frame
    // A participant's own settings, its ⋯ menu: Hash, Threads, nodes or depth, time odds, its own book; then moves.
    enum class ParticipantAction { None, Again, Top, Up, Down, Remove };
    ParticipantAction participantMenu(ParticipantSetup& p, int index, bool open_book, bool& changed);
    // The library beside the participants (or beside A and B of a match): searched by name, group and notes, a group
    // chosen with its chip, sorted by name or reference Elo, the engines with a problem marked. Double-click or Enter
    // adds the engines selected (Ctrl and Shift select several), a drag drops them at a place in the participants;
    // opponents suggested around the first participant's rating. True when the setup changed.
    bool participantPicker();
    struct EngineLibrary {
        std::string search;
        std::string group;                    // the group whose chip is on, empty: every engine
        ImGuiSelectionBasicStorage selected;  // by ImHashStr of the engine's name
        bool focused = false;                 // one of its engines had the keyboard at the last frame: Enter adds
        int suggest  = 8;                     // opponents to suggest
        int range    = 150;                   // Elo, around the first participant's
        std::string message;                  // what the last suggestion found
    } library_;
    std::vector<std::string> library_drag_;  // the engines being dragged from the library to the participants
    float players_list_h_ = 0;               // the height of the participants' and the library's lists
    // Library engines added: A then B of a match, before participant `at` otherwise (-1: at the end).
    bool addParticipants(const std::vector<std::string>& names, int at = -1);
    int open_picker_ = 0;  // --focus setup-picker: 1 the library's search focused, 2 with opponents suggested
    // A participant's own book: a small button ("—", or the book's name) that opens the choice of the library's
    // Polyglot books and of the depth. True when `book` changed; `again` (when given) is set when the user asked
    // for the engine once more, with another book.
    bool ownBookButton(const char* id, OwnBook& book, float width, bool* again = nullptr, bool open = false);
    // Why a book cannot be a participant's own (ownBookProblem()), kept by path and read again when the file's size
    // or time changes; empty when it can, or when it is not found. Polyglot books only: a PGN book's problems are
    // those of its conversion (pgn_books_).
    const std::string& ownBookCheck(const std::string& path);
    // The participants' PGN books turned into Polyglot books, in the background.
    PgnBookJobs pgn_books_;
    // At each frame: the PGN books of the setup are wanted (prepared as soon as chosen or loaded), the conversions
    // move on, and those that ended say so in the console.
    void pollOwnBooks();
    struct OwnBookCheck {
        uintmax_t size = 0;
        std::filesystem::file_time_type time{};
        std::string problem;
    };
    std::map<std::string, OwnBookCheck> own_book_checks_;
    int open_own_book_       = -1;     // the participant whose book popup opens: --focus setup-book, a copy just added
    bool open_own_book_list_ = false;  // --focus setup-book-list: with its list of books open
    void drawSetupSummary();
    // The actionable messages of the setup and their fixes, see view_notices.cpp: the summary's checks of the
    // threads, the option values, the opening book and the tablebases (read off the UI thread, tb_checks_).
    [[nodiscard]] std::vector<Notice> setupNotices();
    void drawSetupNotices(std::vector<Notice> notices, const char* id);  // with the fixes possible now
    void applyFix(const Fix& fix);
    void selectEngine(const std::string& name);  // on the Engines page, as the palette opens an engine
    TablebaseChecks tb_checks_;
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

    // The rows of the setup's settings, each folded to a line: its profile, its summary, and ✎ that opens its
    // details in place. The five sections with a profile, then the concurrency and the PGN files of this tournament.
    enum SetupRow { RowFormat, RowTime, RowOpenings, RowAdjudication, RowEngines, RowConcurrency, RowPgn, ROW_COUNT };
    std::array<bool, ROW_COUNT> setup_open_{};         // the rows whose details are open
    std::array<float, ROW_COUNT> setup_details_h_{};  // their height at the last frame, for their background
    float setup_title_w_ = 0;                          // the column of their titles
    // The row of a name: "format", "time", "openings", "adjudication", "engines", "concurrency", "pgn".
    [[nodiscard]] static std::optional<SetupRow> setupRowOf(const std::string& key);
    int open_profile_list_ = -1;  // --focus setup-profiles=ROW: the row whose profile list opens
    // One row, then its details drawn by `details` under it when it is open. `section`: the profile's, none for the
    // rows without one; `more`: what the summary's tooltip adds to it.
    void setupRow(SetupRow row, const char* title, const std::string& summary, const std::string& more,
                  std::optional<SetupSection> section, void (App::*details)());
    // A section's row, its summary and its tooltip made from the setup.
    void sectionRow(SetupRow row, SetupSection section, void (App::*details)());
    void drawSetupRows();
    // The rule that chose the number of games played at once: one per hardware thread for a node count, physical
    // cores − 1 for a clock, or the user's; `brief` for the row's summary.
    [[nodiscard]] std::string concurrencyRule(bool brief) const;
    [[nodiscard]] std::string concurrencySummary() const;  // "15 games at a time · physical cores − 1, for a clock"
    [[nodiscard]] std::string pgnSummary() const;
    // The profile list of a section: a profile is applied when chosen; then Save as profile…, and Update, Revert and
    // Delete for the profile the section came from.
    void profileCombo(SetupSection section, float width);
    // Under the details of a section: Save as profile…, and Update the profile and Revert when it was changed for this
    // tournament only.
    void profileActions(SetupSection section);
    // The popups of Save as profile… (its name) and of Delete (the confirmation), once per frame.
    void drawProfilePopups();
    void applySetupProfile(SetupSection section, const SetupProfile& profile);  // to the setup, saved soon
    void saveSetupProfile(SetupSection section, const std::string& name);       // the section as profile `name`
    void saveProfiles() { storage_.saveProfiles(profiles_.toJson()); }
    std::string profile_name_;                        // Save as profile…: the name typed
    SetupSection profile_section_ = SetupSection::Format;  // the section it saves, or Delete removes from
    bool open_save_profile_       = false;                 // its popup opens on the next frame
    std::string profile_delete_;                           // the profile Delete asks to confirm, opened next frame

    // test suites
    void drawSuites();
    void drawSuiteGrid();
    void loadSuiteSetup();  // suite.json and its suite, once
    void saveSuiteSetup();  // to suite.json
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
    // The Create menu of the list: books the program writes.
    void drawBookCreateMenu();
    // Where a book the program writes is offered to go, named `name`: next to the book chosen in the list, else the
    // last one of the library, else in <profile>/books (made now, for the dialog to open in).
    std::string newBookPath(const std::string& name);
    // A book the program wrote: added to the library and chosen, or read again when the library has it already.
    void addWrittenBook(const std::string& path, const std::string& description);
    // Asks where to write a Chess324 book, every position or the symmetrical ones (tour/chess324.hpp).
    void askChess324(bool symmetrical);
    void writeChess324Book(std::string path, bool symmetrical);
    bool open_book_create_ = false;  // --focus books-create: the Create menu open
    // Build a Polyglot book…: its form in the detail pane, the build in the background (tour/pgn_book.hpp).
    struct BookBuildForm {
        std::string pgn;
        int plies     = 20;
        int min_games = 3;
        bool by_score = true;
        bool rated    = false;
        int min_elo   = 2400;
    };
    void openBookBuild();  // shows the form, with the PGN book chosen in the list if it is one
    void drawBookBuild();
    void startBookBuild(std::string bin);
    void pollBookBuild();  // at each frame: the build that ended, added to the library
    [[nodiscard]] tour::PgnBookOptions bookBuildOptions() const;
    BookBuildForm book_build_form_;
    bool book_build_shown_ = false;  // the detail pane shows the form rather than the book chosen
    std::string book_build_error_;   // why the last build failed, shown in the form
    PolyglotBuild book_build_;

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
    // done again later; it waits for a test suite to end, and for the PGN books it plays (`books`) to be turned into
    // Polyglot books (see startGate). Meanwhile no other read or check starts.
    // `key` names what it starts: asked again while it waits, nothing changes; asked while another start waits, it
    // is refused (see startAsked). `failed`, if any, is called instead of `start` when the read of an engine it needs
    // fails, or one of its books cannot be prepared. True when `start` was put off (or refused).
    bool startAfterReads(const std::string& key, std::vector<std::string> needs, std::vector<OwnBookNeed> books,
                         std::function<void()> start, std::function<void()> failed = {});
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
    // A rating list (a list site's text or page, Ordo, CSV) whose ratings become the reference Elo of the library
    // engines it names, once seen in a preview.
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

    // Programs rebuilt or replaced (view_changes.cpp).
    void showEngineChanges(const std::string& name);  // the engine's card, with what changed
    // The choice for every option of the engine's notice that waits for one; nullopt dismisses the notice. Saved.
    void settleEngineChanges(const std::string& name, std::optional<ValueChoice> choice);
    // What the new read of the engine's rebuilt program found, on its card, with the choices (disabled if `locked`).
    void drawProgramChanges(int index, bool locked);
    // The setup summary's warnings: the engines named whose program changed since their last tournament. A separate
    // function, so that the summary's other lines can change around it.
    void drawRebuiltSincePlayed(const std::vector<std::string>& names);
    // The header's notice of a resumed tournament whose participants' programs changed since they joined it.
    void drawResumedPrograms();
    std::map<std::string, PlayedProgram> played_programs_;  // by programKey(), from tournaments_
    std::vector<ParticipantProgram> resumed_programs_;      // of the tournament resumed, until Continue or Pause

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
    // The engines of the open tournament that cannot start (tour::StartFailures): which one, the error, its engine
    // log, and Try again, Withdraw…, Replace…. A card of its own, under `title` when given; nothing when every engine
    // starts. The tournament header and the home page show it alike.
    bool drawStartFailures(const char* title = nullptr);
    // An engine set aside, back, or the tournament stopped for want of one: told once each, in the log, and on the
    // desktop when an engine is set aside.
    void watchStartFailures(OpenRun& open);
    // Starts the process playing the tournament in `dir` (created already, or to resume), in a new tab; an error
    // message, empty on success.
    std::string launchTournament(const std::filesystem::path& dir);
    // Those incidents by engine, the most first: what happened to it, with Open the engine and its tournament's folder.
    [[nodiscard]] std::vector<Notice> engineIncidentNotices(const tour::Incidents& incidents) const;
    // About how long the running tournament has left at its pace; nullopt without a game count or a pace yet.
    [[nodiscard]] std::optional<double> runSecondsLeft() const;
    // "Relaunch with…" of a finished match between `engine_a` and `engine_b`, played from the folder `from`: the
    // same match with another engine A. `open`: its popup opens now (the command palette).
    void drawRelaunch(const std::string& engine_a, const std::string& engine_b, const std::filesystem::path& from,
                      bool open = false);
    void resetRunView();  // forgets the games and statistics shown, before a run starts
    void clearRunView();  // the games, statistics and selections the views show, forgotten
    void pollRun();
    void onRunFinished();
    std::string runStatusText() const;
    std::string summaryText() const;
    // How the tournament's SPRT ended: H1, H0, or not by a decision (SPRT_CONTINUE: stopped, or its games over).
    // The LLR may be beyond a bound without one, before the minimum of games.
    [[nodiscard]] fastchess::SPRTResult sprtVerdict() const;

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
    bool software_at_launch_ = false;  // the Settings page says that a change applies at the next launch

    std::vector<EngineEntry> engines_;
    MatchSetup setup_;
    // The profiles of the setup's sections (profiles.json), with the program's presets.
    ProfileStore profiles_;
    bool setup_dirty_ = false;
    std::chrono::steady_clock::time_point last_setup_save_{};

    // The tournaments open in the workspace, a tab each: those playing, each in a process of its own (RemoteRun), the
    // quick test (played here, RunController), and those opened to look at them. The views show `run_`, the tab
    // chosen; what they showed of another tab is kept with it (OpenRun) while it is not shown.
    std::vector<OpenRun> runs_;
    std::size_t shown_run_ = 0;
    RunController idle_run_;  // what the views read while no tournament is open
    Run* run_ = &idle_run_;
    [[nodiscard]] OpenRun* shownRun() { return shown_run_ < runs_.size() ? &runs_[shown_run_] : nullptr; }
    // The tab of the tournament in `dir`, -1 when none.
    [[nodiscard]] int findRun(const std::filesystem::path& dir) const;
    [[nodiscard]] int runsPlaying() const;  // tournaments playing, the quick test included
    // A tournament plays in this program (the quick test): fastchess' globals are taken, and its engines must not be
    // ended by an engine read interrupted for a start.
    [[nodiscard]] bool localPlaying() const;
    // A new tab, shown at once (the views start over from it); returns its index.
    std::size_t addRun(OpenRun run);
    // The workspace shows tab `index`: what the views showed of the tab before is kept with it.
    void showRun(std::size_t index);
    // Closes the tab of a tournament that does not play.
    void closeRun(std::size_t index);
    void stashRunView();  // what the views show of the shown tab, into its OpenRun
    void loadRunView();   // and back, then the views start over from its tournament
    void drawRunTabs();   // the tabs, above the workspace's header
    // The home page's card of a tournament playing that is not shown: what its copy and its process tell.
    home::RunCard runCardOf(std::size_t index);
    // Who leads a match, from A's point of view (`stats`, `point`): the LLR of its SPRT with the Elo and the score, or
    // the Elo of the engine ahead with the score. For its card on the home page.
    static std::pair<std::string, std::string> matchLeader(const RunInfo& info, const fastchess::Stats& stats,
                                                           const StatPoint& point, const fastchess::SPRT* sprt);
    // The controls of tab `index` on the home page's card: pause or resume, stop, its weight against the others'.
    void drawRunControls(std::size_t index);
    // The machine's budget (Settings, MachineBudget): what the tournaments share, its defaults resolved.
    MachineBudget budget_setting_;
    uint64_t installed_mb_ = 0;  // the machine's memory, read once
    [[nodiscard]] tour::Budget machineBudget() const;
    // The engine threads of the window's own engines: the quick test's games, the analysis engine of the Board page.
    [[nodiscard]] int localThreads() const;
    // The engine threads of the Board page (its analysis, its games; boardThreads()), counted with the cores the
    // tournaments share.
    [[nodiscard]] int analysisThreads() const;
    // The share of each tournament playing, by weight (budget.hpp): their allowances, sent to their processes when
    // they change. Every frame: a tournament started, ended, paused, its weight changed, the budget changed.
    void arbitrate();
    // The cores of the budget no tournament playing can take (each at its concurrency), nor the window's engines.
    [[nodiscard]] int freeCores() const;
    // The queue may start its next tournament beside those playing (MachineBudget::queue_when_free).
    [[nodiscard]] bool queueBeside(std::size_t index) const;
    // What a tournament about to start gets (with the weight `weight`), beside those playing.
    [[nodiscard]] tour::Allowance allowanceFor(const tour::TournamentSpec& spec, double weight) const;
    // Settings: the machine's budget.
    void drawMachineBudget(float label);
    int tab_select_frames_ = 0;  // the tab bar selects the tab shown (showRun() from elsewhere) for a few frames
    // What runStatusText() says of tab `index`, shown or not.
    [[nodiscard]] std::string runStatusText(std::size_t index) const;
    // Connects to the process playing the tournament in `dir` without the window (detached), in a new tab, with that
    // weight; false when none listens there.
    bool attachTournament(const std::filesystem::path& dir, double weight);
    // Why the tournament in `dir` cannot be resumed (finished, a book that cannot be used, played elsewhere); empty
    // when it can.
    [[nodiscard]] std::string resumeCheck(const std::filesystem::path& dir);
    // A tournament of a tab not shown ended: told, its queue goes on, as onRunFinished() does for the shown one.
    void onBackgroundRunFinished(std::size_t index);
    Dialogs dialogs_;
    PieceAtlas atlas_;
    Board3d board3d_;  // the Board page's, when appearance::current().board3d.on

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
    std::vector<int> live_shown_;  // the slots the live view shows this frame (shownSlots)
    std::vector<SlotView> slot_views_;
    bool live_mosaic_       = true;
    GraphView live_graph_;    // the Live view's graph: its tab, hidden curves, folded
    GraphView replay_graph_;  // the Games page's

    // Big screen.
    struct {
        bool on = false;  // in the main window
        BigView view;
        std::chrono::steady_clock::time_point moved{};  // the mouse moved: the controls show for a few seconds
        bool made_full = false;  // the main window went full screen with it, and leaves full screen with it
    } big_;
    std::map<int, BigView> big_boards_;             // the detached boards shown as a big screen, by their slot
    std::map<int, SDL_Window*> big_board_windows_;  // the window each is in (the main one when it is docked there)
    int big_board_focused_       = -1;              // the one focused last, which F11 and Esc are for while it is
    int big_board_focused_frame_ = -1;              // the frame it was seen focused

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
    // The verdict of the tournament's Syzygy tables (its adjudication's, else those forced on its engines) on `board`,
    // once probed; nullptr until then, or when they say nothing of it. Each position is probed once, off the GUI
    // thread, one at a time (live_tables_).
    [[nodiscard]] const TableVerdict* liveVerdict(const chess::Board& board);
    struct LiveTables {
        std::string paths;
        bool ignore50 = false;
        std::map<std::string, std::optional<TableVerdict>> known;  // by FEN; nullopt: the tables do not know it
        std::future<std::optional<TableVerdict>> job;
        std::string job_fen;
    } live_tables_;

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
    int opening_show_ = 0;  // 0 all, 1 a colour won both games, 2 drawn twice, 3 decisive, 4 flagged unbalanced
    bool export_balanced_only_ = true;     // Export as EPD… leaves out the openings flagged unbalanced
    std::vector<int> opening_order_;       // rows shown, sorted
    uint64_t openings_replaced_seen_ = 0;  // the tournament's openingsReplaced() the views were refreshed for
    uint64_t network_voids_seen_ = 0;  // games voided for a machine's broken connection, as last seen
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
    bool book_rows_dirty_  = true;
    int book_item_         = -1;     // opening shown on the board
    int book_item_request_ = -1;     // --focus books-item=N: that opening shown once the book is read
    bool drop_mirrored_    = false;  // the cleaned copy also leaves out mirrored positions

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
        bool waiting_book  = false;    // ... or the participant whose book is being prepared
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

    // The engine console, while one is open.
    std::unique_ptr<EngineConsole> engine_console_;

    // The Board page, and where it was opened from (Back).
    BoardPage board_;
    struct BoardBack {
        Page page;
        std::optional<Tab> tab;
    };
    std::optional<BoardBack> board_back_;
    bool board_key_taken_  = false;  // key A served a board this frame
    bool review_when_read_ = false;  // --focus board-review: the game analysed once its file is read
    int review_ply_        = -1;     // --focus board-review-PLY: the board at that ply then

    // Console view.
    std::vector<Console::Line> console_lines_;
    uint64_t console_version_ = 0;
    std::string console_source_;                // the tournament whose lines are shown; empty: all; "-": the window's
    std::vector<std::string> console_sources_;  // the tournaments the lines come from
    std::vector<int> console_shown_;            // the lines shown, by index
    bool console_follow_ = true;

    // Standings of a round robin or gauntlet, recomputed when games arrive.
    tour::Table table_;
    tour::Table shown_;  // table_ in the order of the Standings tab, which its copies and export follow
    tour::TeamTable teams_;  // a team match: the teams' scores, computed with table_
    tour::Order standings_order_ = tour::Order::Points;
    std::size_t table_for_       = ~std::size_t(0);
    std::chrono::steady_clock::time_point table_next_{};  // a big table waits a while before it is computed again
    std::optional<tour::Anchor> anchor_;
    int crosstable_mode_ = 0;                             // 0 score, 1 wins/draws/losses, 2 Elo difference
    int standings_view_  = -1;  // 0 table, 1 crosstable, 2 teams (a team match), -1 as suits the tournament
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
    std::string template_loaded_;   // the template the setup came from, named in the menu; empty: none
    bool open_save_template_ = false;  // Save the setup as a template… asked from the command palette
    // Templates loaded by name (the template menu, the command palette): the sections that played as their profile
    // take its values of today. False, with template_message_, when it cannot be read.
    bool loadTemplate(const std::string& name);
    void listTemplates();  // template_names_, listed again every few seconds

    // Clone: the New tournament page filled with the setup the tournament of `dir` started from, every value as it
    // played, to change before it starts. False when it has none (see readTournamentSetup).
    bool cloneTournament(const std::filesystem::path& dir);
    // Whether that setup is there, looked at once per folder: the buttons of a list ask it every frame.
    bool canClone(const std::filesystem::path& dir);
    // The Clone button of a tournament (`compact`: in a table's row), greyed with the reason when it cannot.
    void drawClone(const std::filesystem::path& dir, bool compact = false);
    std::map<std::string, bool> clone_sources_;  // canClone() by folder; cleared when the list is read again

    // Schedule tab and edits.
    int edit_kind_ = 0;        // tour::Edit::Kind
    std::string edit_engine_;  // engine list entry to add or bring in
    // Its settings of its own, when given (0, 100 %: the tournament's); otherwise the tournament's.
    bool edit_own_settings_ = false;
    ParticipantSetup edit_own_;
    std::string name_draft_of_;  // engine whose Name field is being edited, and the name typed so far
    std::string name_draft_;
    bool edit_catch_up_     = true;
    int edit_team_          = 0;   // a team match: the team a newcomer joins
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
    // The game menu's "Set the result": the game it is open for (number()), the result chosen ("1-0", "1/2-1/2",
    // "0-1"), the reason typed, and the impact of the change, computed again when the choice or the tournament
    // changes.
    struct ResultMenu {
        uint64_t game = ~uint64_t{0};
        std::string result;
        std::string why;
        std::string key;
        tour::Impact impact;
    } result_menu_;
    // The live game menu's "End with result": the game it is open for (game_id), the result chosen, the reason, and
    // why the last try failed.
    struct EndMenu {
        std::size_t game = 0;
        std::string result;
        std::string why;
        std::string error;
    } end_menu_;
    uint64_t result_set_game_ = ~uint64_t{0};  // the game whose result was set last, while that can be undone
    uint64_t open_game_menu_  = 0;  // the game (game_id) whose menu opens in the Games tab (palette, --focus)
    int open_end_menu_        = 0;  // the slot + 1 whose board opens its "End with result" menu (palette)
    // The results of games asked for by the views (gameResult()), by number(); dropped when the tournament changes.
    std::map<uint64_t, std::optional<tour::GameResult>> result_cache_;
    uint64_t result_cache_version_ = ~uint64_t{0};
    const void* result_cache_of_   = nullptr;

    // Queue and relaunch.
    std::vector<QueuedTournament> queue_;
    std::chrono::steady_clock::time_point queue_checked_{};  // the queue looked at for cores free
    bool queue_held_    = false;  // Pause all: the queue starts no tournament until Resume all
    bool queue_blocked_ = false;  // a tournament ended while it was held: Resume all starts the next one
    std::string relaunch_engine_;
    std::filesystem::path relaunch_from_;     // the folder of the match whose Relaunch with… started last
    std::filesystem::path relaunch_request_;  // its popup opens on the home page (the command palette)

    // The home page's control room, computed again only when what it shows changed.
    struct CoresCache {
        const tour::TournamentState* of = nullptr;
        uint64_t version                = ~uint64_t(0);
        std::size_t names               = 0;
        std::vector<int> threads;  // by the observer's participant index
    } cores_cache_;
    struct LeaderCache {
        std::size_t games = ~std::size_t(0);
        Clock::time_point started{};
        std::string leader, detail;
        double games_per_hour = 0;  // the cards of the tournaments not shown
    } leader_cache_;
    // The same for the cards of the tournaments not shown, from their copy of the journal: by folder.
    std::map<std::string, LeaderCache> other_leaders_;
    std::string finish_hidden_;  // the finish message whose card under Needs action was hidden
    std::future<std::vector<home::DiskSpace>> disk_job_;
    std::vector<home::DiskSpace> disks_;
    std::chrono::steady_clock::time_point disk_asked_{};

    // Broadcast.
    Broadcaster broadcaster_;
    bool broadcast_editing_ = false;  // a folder, token or round field was being typed in at the last frame
    QrCode tunnel_qr_;                // of the tunnel's public address
    std::string tunnel_qr_text_;

    // Ratings page: every game of the profile, and what is computed from them, on a worker thread: a rating list
    // of thousands of engines takes a moment.
    struct RatingsResult {
        std::shared_ptr<const tour::GameDb> db;
        std::string anchor;  // the one used: the default when none was chosen
        double anchor_elo = 0;
        tour::RatingList list;
        tour::RatingListInfo info;  // how `list` was made
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
    tour::LibraryAliases game_db_aliases_;      // and the names the user gave to library engines
    bool open_pgn_players_ = false;
    // Open: the choices made in it are read with the games once it closes, not at each one.
    bool pgn_players_shown_ = false;
    // A PGN file added: 1, the games are to be read with it; 2, they are being read. Once read, the players that
    // two library engines may be, or one by a hint, are shown for the user to choose (see reviewPgnPlayers()).
    int review_pgn_players_ = 0;
    std::set<std::string> pgn_players_before_;  // the players offered before the file was added, folded
    std::optional<std::vector<std::string>> separate_players_;  // settings.json's, read once
    std::vector<std::string>& separatePlayers();
    std::string pgn_players_filter_;
    bool ratings_dirty_ = true;  // compute again, with other filters
    std::optional<std::vector<std::filesystem::path>> rating_pgns_;  // settings.json's, read once
    const std::vector<std::filesystem::path>& ratingPgns();
    void setRatingPgns(const std::vector<std::filesystem::path>& files);
    tour::RatingList rating_list_;
    tour::RatingListInfo rating_info_;  // how it was made, for the header of its exports
    bool rating_copy_header_ = false;   // the header on top of the copies too
    std::vector<int> rating_order_;  // the rows of the list as shown, sorted by a column
    bool rating_order_dirty_ = true;
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
    // What fixes them (an engine to open, a program to locate…), while they are the messages they were found for;
    // Open the log for any other start error. See drawMessage().
    std::vector<Fix> start_fixes_, finish_fixes_;
    std::string start_fixes_for_, finish_fixes_for_;
    // A start or end message as a notice (red when `error`), with its fixes when `fixes_for` is still `text`.
    void drawMessage(const std::string& text, const std::vector<Fix>& fixes, const std::string& fixes_for, bool error,
                     const char* id);
    void setStartFixes();        // the fixes of the setup's errors, after a start that failed
    void setFinishFixes();       // the engine that stopped the tournament, at its end
    std::string stopped_by_;     // that engine, as the tournament names it; empty when none did
    uint64_t console_mark_ = 0;  // the log's lines before the tournament started (Console::added())
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

    // New versions (update_check.hpp): the check at launch, a notice on the home page, the version in Settings.
    void startUpdateCheck(bool asked);  // `asked`: from Settings, its result shown there
    void pollUpdateCheck();
    void drawUpdateNotice();
    void drawVersionSettings();
    void copyVersion();
    update::Settings updates_;
    update::Checker update_checker_;
    bool update_asked_ = false;  // the check running was asked from Settings
    std::string update_status_;  // what the check asked from Settings found
    bool update_notes_open_ = false;

    // "Report a problem…" (report.hpp): a new GitHub issue in the browser, filled with the version, the system and
    // the log; `seen`, when given, fills what went wrong.
    void reportProblem(const std::string& seen = {});
    bool reportButton(const char* id, bool small = false, const std::string& seen = {});  // the button, its tooltip
    [[nodiscard]] std::string systemReport() const;

    // Appearance (view_appearance.cpp): the boards' colours, the pieces and the move sounds, in Settings; the
    // engines' logos, on their cards.
    void loadAppearance();  // at startup, from settings.json
    // `settings` in force and saved; the pieces made again at the start of the next frame when they changed.
    void applyAppearance(const appearance::Settings& settings);
    void rebuildPieces();  // between frames: the last frame may still draw the old atlas
    void drawAppearanceSettings(float label);
    void drawBoardPreview(ui::Theme theme, float size);
    // A sound for each new move of the game in `slot`, when it is the one followed (Live, the big screen).
    void hearMoves(int slot);
    bool pieces_dirty_ = false;
    std::string pieces_error_;  // why the folder's pieces are not those drawn, empty when they are
    // settings.json written a moment after the last change: not at every step of a colour dragged in its picker.
    std::optional<std::chrono::steady_clock::time_point> appearance_save_due_;
    ui::Theme appearance_theme_ = ui::Theme::Light;  // the theme whose board the Settings page edits
    bool appearance_theme_set_  = false;             // chosen once, then left as the user put it
    bool scroll_to_appearance_  = false;             // asked from the command palette
    struct Heard {
        std::size_t game  = 0;
        std::size_t plies = 0;
        int frame         = -1;  // when: a game followed again after a while makes no sound for what it played since
    };
    std::map<int, Heard> heard_;  // by slot
    int heard_frame_ = -1;        // the frame a sound was last played: one at a time

    // The engines' logos, as textures made when first drawn; by file name in the logos folder. A small one too, for
    // SDL's renderer, which has no mipmaps to draw the large one small.
    struct LogoTexture {
        ImTextureID large = ImTextureID_Invalid;
        ImTextureID small = ImTextureID_Invalid;
        int width = 0, height = 0;  // the large one's: the proportions
        std::string error;          // why it cannot be shown, empty when it can
        [[nodiscard]] ImTextureID texture(float pixels) const;
    };
    [[nodiscard]] const LogoTexture* engineLogo(const std::string& engine);  // nullptr: none, or unreadable
    // The engine's logo as an item `height` high, its proportions kept; its width, 0 (and nothing drawn) without one.
    float drawEngineLogo(const std::string& engine, float height);
    void drawLogoRow(int index, float label);  // the engine card's row
    void setEngineLogo(int index, const std::string& picture);
    void removeEngineLogo(int index);
    void forgetLogoFile(const std::string& file);  // its texture freed, the file removed when no engine has it
    std::map<std::string, LogoTexture> logo_textures_;
    std::vector<ImTextureID> stale_textures_;  // freed at the start of the next frame: this one may draw them
    std::string logo_error_;      // the last choice of a logo that failed
    std::string logo_error_for_;  // the engine whose card shows it
    int logos_version_     = 0;   // changed with each logo set or removed
    int web_logos_version_ = -1;  // the version the web page's logos were made from

    // Remote machines (view_machines.cpp, machines.hpp): their cards in Settings, the jobs running on them, and the
    // passphrases of key files typed this session, in memory only.
    struct MachineActivity {
        std::unique_ptr<MachineJob> job;       // a test, Install, the engines' check, the files compared
        std::unique_ptr<MachineJob> transfer;  // books and tablebases copied, which may take hours, beside the others
        std::optional<JobResult> last;         // what the last job found, shown on the card
        MachineJob::Kind last_kind = MachineJob::Kind::Test;
        std::optional<JobResult> last_transfer;
        std::optional<FilesCheck> files;  // the last comparison, what a push sends
        std::string message;              // what keeps a job from starting (the machine incomplete, a key file)
        bool remove_asked = false;
        // An engine copied there (Copy to the machine, Replace with this computer's): the copy offered, its panel
        // open under the engines, then copied by the transfer job; and what the last copy said.
        std::optional<EngineCopy> copy;
        std::optional<JobResult> last_copy;
        bool open_engines  = false;  // the engines' list opened at the next frame (a copy asked from elsewhere)
        int scroll_to_copy = 0;      // and the page scrolled to the copy's panel, these frames (the card grows first)
    };
    void loadMachines();
    void saveMachines();
    void forgetPassphrases();  // wiped
    void pollMachines();
    void drawMachineSettings(float label);
    void drawMachineCard(std::size_t index);
    void drawMachineEngines(Machine& machine, MachineActivity& activity);
    void drawMachineFiles(Machine& machine, MachineActivity& activity);
    void drawEngineCopy(Machine& machine, MachineActivity& activity);
    // The copy of the library's engine `engine` to the machine `id` offered on its card (Settings opened there),
    // `build` the file chosen for the machine instead of the program known.
    void offerEngineCopy(const std::string& id, const std::string& engine, const std::filesystem::path& build = {});
    // This computer's program of each engine of the machine's list, for the builds' comparison.
    [[nodiscard]] std::map<std::string, std::filesystem::path> machinePrograms(const Machine& machine) const;
    // The format of a program of this computer, read again when the file changes.
    [[nodiscard]] const ProgramFormat& programFormatOf(const std::filesystem::path& program,
                                                       const tour::FileFingerprint& stamp);
    std::map<std::string, std::pair<tour::FileFingerprint, ProgramFormat>> program_formats_;
    float copy_panel_y_ = -1;  // where the copy's panel was drawn, the page scrolled there once (screen, -1: no)
    void drawPassphrasePrompt();
    // Starts a job on the machine `id`, its key's passphrase asked first when it has one not typed yet.
    void startMachineJob(const std::string& id, MachineJob::Kind kind);
    void addMachine();
    [[nodiscard]] Machine* machineById(const std::string& id);
    [[nodiscard]] FilesRequest setupFiles() const;  // the setup's books and tablebases
    // Tournaments on a machine: the one in `dir` started (or resumed) there and shown in its tab, or the window
    // connected to the one playing there already. An error message, empty on success.
    std::string launchOnMachine(const std::filesystem::path& dir, const std::string& machine_id);
    bool attachOnMachine(const std::filesystem::path& dir, double weight);
    // The tab of a tournament on a machine, its process reached through `link`.
    std::string openMachineRun(const std::filesystem::path& dir, const Machine& machine, const std::string& remote_dir,
                               std::unique_ptr<SshLink> link, std::shared_ptr<LinkStatus> status, double weight);
    // The passphrase of a machine's key, empty when it needs none or it was not typed yet (`missing` then).
    [[nodiscard]] std::string machinePassphrase(const Machine& machine, bool& missing);
    void pollMachineRuns();       // their files' last copy once they end, what their start said
    void arbitrateMachines();     // each machine's budget shared by its tournaments
    void drawRunOn(float label);  // the setup's Run on, its label `label` wide
    std::vector<Machine> machines_;
    std::map<std::string, MachineActivity> machine_activity_;  // by machine id
    std::map<std::string, std::string> machine_passphrases_;   // by key file
    std::string machine_open_;        // the machine whose card is open
    std::string machine_dialog_for_;  // the machine a file dialog was opened for
    std::string machine_dialog_engine_;  // and the engine, for its build for the machine
    std::string passphrase_machine_;  // the machine whose key's passphrase is asked, and the job then started
    MachineJob::Kind passphrase_kind_ = MachineJob::Kind::Test;
    bool open_passphrase_             = false;
    std::array<char, 256> passphrase_input_{};
    std::function<void()> passphrase_then_;  // run once the passphrase is typed, instead of a machine job

    // The engines of the library that run on a machine (engine_hub.hpp): the window's hub, for their reading, checks,
    // console, quick test, analysis and games here; started with the first such engine.
    std::unique_ptr<EngineHub> hub_;
    std::string hub_signature_;  // the engines it was last told it may start
    void syncEngineHub();
    // Before an engine that runs on a machine starts: false when its key's passphrase must be typed first (asked
    // then, `then` run once it is).
    bool remoteReady(const EngineEntry& e, std::function<void()> then);
    // A passphrase typed or refused, for the machine jobs and the hub alike (empty: forgotten).
    void rememberPassphrase(const std::string& key_file, const std::string& passphrase);
    // Why a read or a check of `e` failed, told by the hub when its machine could not be reached.
    [[nodiscard]] std::string remoteFailure(const EngineEntry& e) const;
    // Under the home page's cores: the machines the running tournaments' engines run on, and how they do.
    void drawRemoteMachinesLine();
    // The library's engine of that name, -1 when there is none (any more).
    [[nodiscard]] int engineIndex(const std::string& name) const;
    // The passphrases a tournament's engines on machines need, by key file, as far as they were typed.
    [[nodiscard]] std::map<std::string, std::string> secretsFor(const tour::TournamentSpec& spec);
    // Before a start: false when a machine of the setup's engines needs its key's passphrase first (asked, and
    // `then` run once typed).
    bool remoteEnginesReady(const std::vector<std::string>& engines, std::function<void()> then);

    // The card's Runs on: this computer, or a machine; shown once there are machines (or the engine runs on one).
    // True when it changed the engine.
    bool drawRunsOn(EngineEntry& e, float label);
    // `e` moved to `machine` (empty: this computer): the program there, or this computer's, taken back.
    void moveEngine(EngineEntry& e, const std::string& machine);
    // The program of an engine that runs on a machine, written in that machine's list of engines.
    void mirrorOnMachine(const EngineEntry& e);
    // A new engine of the library: the program `path` on `machine`, read at once.
    void addRemoteEngine(const std::string& machine, const std::string& path);
    // What is known of a machine an engine runs on, in a few words, and their colour.
    struct MachineLine {
        std::string text;
        unsigned color = 0;
    };
    [[nodiscard]] MachineLine machineLine(const std::string& id) const;

    // The files of a machine, looked through over SFTP in the page: to add an engine there, choose its program, its
    // folder, a file its options name, or the program of the machine's list of engines.
    struct RemoteListing {
        std::string home;  // the user's home folder there, shown as ~
        std::string dir;   // where the server says it is
        std::string file;  // the path typed is a file: it is chosen
        std::vector<RemoteEntry> entries;
        std::string error;
    };
    struct RemotePicker {
        enum class Purpose { AddEngine, ImportFolder, Program, Folder, Option, OptionFolder, MachinePath };
        Purpose purpose = Purpose::AddEngine;
        std::string machine;  // its id
        std::string engine;   // the engine it chooses for
        std::string option;   // Option: its name
        bool folders = false;  // a folder is chosen, not a file
        bool shown   = false;
        bool untested = false;  // the machine's host key is not known yet
        bool hidden   = false;  // the files whose name starts with a dot shown
        std::string home;  // known after the first listing
        std::string dir;
        std::string selected;
        std::array<char, 1024> typed{};
        std::vector<RemoteEntry> entries;
        std::string error;
        std::future<RemoteListing> listing;
    };
    std::optional<RemotePicker> remote_picker_;
    // The SHA-256 of the programs of engines that run on machines, read there in the background: a program rebuilt
    // there is noticed as a local one is (ProgramState::Changed, read again).
    struct RemoteHash {
        std::string engine, machine, program, hash;
    };
    std::future<std::vector<RemoteHash>> remote_hashes_;
    std::vector<std::string> remote_hashes_next_;  // asked for while others are read
    void hashRemotePrograms(std::vector<std::string> names);
    void pollRemoteHashes();
    void openRemotePicker(RemotePicker::Purpose purpose, const std::string& machine, const std::string& engine = {},
                          const std::string& option = {}, const std::string& start = {});
    void listRemote(const std::string& dir);
    void drawRemotePicker();
    void chooseRemote(const std::string& path);
    bool scroll_to_machines_    = false;  // asked from the command palette
    int machines_scroll_frames_ = 0;      // the frames it was asked on
};

}  // namespace fcg
