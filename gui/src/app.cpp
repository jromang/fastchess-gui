#include "app.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <fmt/include/fmt/format.h>
#include <core/globals/globals.hpp>
#include <tour/files.hpp>
#include <tour/sprt_rules.hpp>

#include "i18n.hpp"
#include "memory.hpp"
#include "notify.hpp"
#include "renderer.hpp"
#include "setup_checks.hpp"
#include "sound.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

App::App(SDL_Window* window, Storage storage, CpuTopology topology, AppOptions options)
    : window_(window),
      storage_(std::move(storage)),
      topology_(topology),
      options_(std::move(options)),
      dialogs_(window) {
    software_at_launch_  = storage_.softwareRendering();
    engines_             = storage_.loadEngines();
    books_               = storage_.loadBooks();
    lichess_token_       = storage_.lichessToken();
    lichess_token_saved_ = lichess_token_;
    if (!books_.empty()) selected_book_ = 0;
    setup_ = storage_.loadSetup();
    profiles_.fromJson(storage_.loadProfiles());
    // A setup saved before there were profiles: its sections get those they play like.
    recognizeProfiles(setup_, profiles_);
    i18n::setLanguage(i18n::resolve(setup_.language));
    loadAppearance();

    // The guide panel: its width kept, the `?` of the titles and the messages opening it, links in the browser.
    {
        const auto g            = storage_.guideSettings();
        guide_.width_em         = g.width_em;
        getting_started_hidden_ = g.getting_started_hidden;
        guide_.on_width         = [this](float width) {
            auto settings     = storage_.guideSettings();
            settings.width_em = width;
            storage_.setGuideSettings(settings);
        };
        guide_.open_url = [](const std::string& url) { SDL_OpenURL(url.c_str()); };
        ui::setGuideOpener([this](const char* topic) { guide_.open(topic ? topic : guide::topics::Intro); });
    }

    if (!setup_.concurrency_auto) setup_.concurrency = std::clamp(setup_.concurrency, 1, topology_.logical);

    if (!engines_.empty()) selected_engine_ = 0;

    refreshPrograms();

    // Engines added by hand to engines.json, never read, or read by an earlier version (which may have refused
    // them), get their options read in the background: once per version, not at every start.
    for (const auto& e : engines_) {
        if (e.uci_options.empty() && !e.cmd.empty() && e.read_by != FCG_VERSION) probe_queue_.push_back(e.name);
    }

    Console::instance().add(
        trf("fastchess-gui {} — {} physical cores, {} threads", FCG_VERSION, topology_.physical, topology_.logical));

    unfinished_ = unfinishedMatch(storage_.runsDir());
    queue_      = storage_.loadQueue();
    budget_setting_ = storage_.machineBudget();
    if (const auto memory = systemMemory()) installed_mb_ = memory->total_mb;
    loadMachines();
    layouts_    = layout::named(storage_.loadLayouts());
    if (ImGui::GetCurrentContext()) registerPanelSettings();

    // A new version, asked for at most once a day when the user wants it; the release found last time meanwhile.
    updates_ = storage_.updateSettings();
    if (updates_.enabled && !options_.scripted && update::checkDue(updates_.checked, std::time(nullptr))) {
        startUpdateCheck(false);
    }
}

App::~App() {
    ui::setGuideOpener(nullptr);
    // A read or a check may be waiting on a hung engine, up to a minute or two: its engine is ended, so that its
    // thread ends at once, and the window goes away first rather than freeze while it does.
    if (probe_.valid() || check_.valid()) {
        if (window_) SDL_HideWindow(window_);
        fastchess::atomic::stop = true;  // fastchess' Windows reads give up
        interruptEngineProcesses();      // with what they started (a wrapper script's engine)
        fastchess::stopProcesses();
        if (probe_.valid()) probe_.wait();
        if (check_.valid()) check_.wait();
    }
    // The question to GitHub gives up within its timeout: the window goes away meanwhile.
    if (update_checker_.running() && window_) SDL_HideWindow(window_);
    fingerprints_cancel_ = true;
    if (fingerprints_.valid()) fingerprints_.wait();
    machine_activity_.clear();  // the machines' jobs cancelled, a second or two at most
    forgetPassphrases();
    cancelBookScan();
    for (auto& f : abandoned_scans_) f.wait();
    storage_.saveSetup(setup_);
    storage_.saveEngines(engines_);
    saveLichessToken();  // a token still being typed when the program quits
    if (appearance_save_due_) storage_.setAppearance(appearance::current());
    for (const auto& [file, logo] : logo_textures_) {
        render::destroyTexture(logo.large);
        render::destroyTexture(logo.small);
    }
    for (const auto texture : stale_textures_) render::destroyTexture(texture);
    sound::shutdown();
}

bool App::initGraphics() {
    // The pieces chosen, or the default ones when the folder chosen cannot be used (pieces_error_ says why).
    std::string error;
    const auto svgs = appearance::piecesFor(appearance::current(), error);
    if (error != pieces_error_ && !error.empty()) Console::instance().add(error, true);
    pieces_error_ = error;
    return atlas_.create(160, svgs);
}

void App::requestQuit() {
    if (runsPlaying() > 0) {
        open_quit_modal_ = true;
    } else {
        quit_ = true;
        pending_start_.reset();  // a start put off must not begin on the way out
    }
}

void App::focusWindow(const std::string& name) {
    options_.focus_window = name;
    focus_done_           = false;
}

bool App::debugSelectGame(int index) {
    if (index < 0 || index >= static_cast<int>(games_.size())) return false;
    selectGame(index);
    return true;
}

void App::keepRunningAndQuit() {
    // The quick test plays here: it stops. The others are let go, playing on: their markers stay, and the next launch
    // connects to them again.
    bool local = false;
    for (auto& r : runs_) {
        if (r.run->running() && !r.run->remote()) {
            r.run->stopNow();
            local = true;
        } else if (auto* remote = dynamic_cast<RemoteRun*>(r.run.get()); remote && remote->running()) {
            remote->disconnect();
        }
    }
    if (local) {
        quit_pending_ = true;
    } else {
        quit_ = true;
    }
}

void App::forceQuit() {
    // What this window started stops, as a script that started a tournament (--autostart) expects; the tournaments it
    // found playing at launch play on, as Keep them running and quit lets them.
    bool waiting = false;
    for (auto& r : runs_) {
        if (!r.run->running()) continue;
        if (auto* remote = dynamic_cast<RemoteRun*>(r.run.get()); remote && r.found) {
            remote->disconnect();
            continue;
        }
        r.run->stopNow();
        waiting = true;
    }
    if (waiting) {
        quit_pending_ = true;
    } else {
        quit_ = true;
    }
}

// ---------------------------------------------------------------------------
// Frame

void App::frame() {
    ui::beginTopicFrame();
    // Before the engines are read: nothing is read yet, so a resume at launch starts at once, and the queue reads only
    // the engines it plays first.
    if (!launch_checked_) {
        launch_checked_ = true;
        resumeAtLaunch();
    }
    // Other pieces, logos dropped: before anything is drawn, the last frame drawn with the old ones.
    if (pieces_dirty_) rebuildPieces();
    for (const auto texture : stale_textures_) render::destroyTexture(texture);
    stale_textures_.clear();
    if (appearance_save_due_ && std::chrono::steady_clock::now() >= *appearance_save_due_) {
        appearance_save_due_.reset();
        storage_.setAppearance(appearance::current());
    }
    pollRun();
    pollProbe();
    pollCheck();
    pollStart();
    pollOwnBooks();
    pollBookBuild();
    pollUpdateCheck();
    pollEngineConsole();
    pollBoard();
    pollMachines();
    handleDialogs();

    if (engines_save_due_ && std::chrono::steady_clock::now() >= *engines_save_due_) {
        engines_save_due_.reset();
        storage_.saveEngines(engines_);
    }

    if (options_.autostart && !autostart_done_ && !probe_.valid() && probe_queue_.empty()) {
        autostart_done_ = true;
        // --focus suites: the test suite of the Suites page, instead of a tournament.
        if (options_.focus_window == "suites") {
            loadSuiteSetup();
            startSuite();
        } else {
            startRun();
        }
    }

    handleZoomKeys();
    handleBigScreenKeys();
    applyThemeSetting();
    updateBroadcast();

    // --shot and --focus: show the requested page or tab once the first frames are drawn.
    if (!options_.focus_window.empty() && !focus_done_) {
        const auto& w = options_.focus_window;
        if (w == "setup")
            page_ = Page::NewMatch;
        else if (w == "setup-picker" || w == "setup-suggest") {
            page_        = Page::NewMatch;
            open_picker_ = w == "setup-picker" ? 1 : 2;
        } else if (w == "setup-book" || w == "setup-book-list") {
            page_               = Page::NewMatch;
            open_own_book_      = 0;
            open_own_book_list_ = w == "setup-book-list";
        } else if (w == "setup-pgn") {
            page_           = Page::NewMatch;
            open_pgn_setup_ = true;
        } else if (w.rfind("setup-open=", 0) == 0 || w.rfind("setup-profiles=", 0) == 0) {
            // setup-open=ROW: that row of the setup opened (format, time, openings, adjudication, engines,
            // concurrency, pgn); setup-profiles=ROW: its profile list.
            page_          = Page::NewMatch;
            const auto row = setupRowOf(w.substr(w.find('=') + 1));
            if (row && w[6] == 'o') setup_open_[*row] = true;
            if (row && w[6] == 'p') open_profile_list_ = *row;
        } else if (w == "statistics") {
            page_            = Page::Tournament;
            tab_request_     = Tab::Overview;
            open_statistics_ = true;
        } else if (w == "sprt-calc") {
            page_           = Page::NewMatch;
            sprt_calc_      = setup_;
            sprt_calc_open_ = true;
        } else if (w == "engines")
            page_ = Page::Engines;
        else if (w == "engines-card") {
            page_            = Page::Engines;
            selected_engine_ = engines_.empty() ? -1 : 0;
        } else if (w == "board" || w == "board-analyse") {  // the Board page; its analysis started
            page_ = Page::Board;
            if (w == "board-analyse") setBoardAnalysis(true);
        } else if (w.rfind("board=", 0) == 0 || w.rfind("board-analyse=", 0) == 0 ||
                   w.rfind("board-review", 0) == 0) {
            // board=FILE: a PGN file (or a FEN, EPD file) on the Board page; board-analyse=FILE, analysed;
            // board-review=FILE: its first game analysed move by move, once read; board-review-PLY=FILE, shown at
            // that ply.
            const auto eq = w.find('=');
            dropFile(w.substr(eq + 1));
            if (w.rfind("board-analyse=", 0) == 0) setBoardAnalysis(true);
            if (w.rfind("board-review", 0) == 0) {
                review_when_read_ = true;
                if (w.rfind("board-review-", 0) == 0) review_ply_ = std::atoi(w.c_str() + 13);
            }
        } else if (w.rfind("board-ply=", 0) == 0) {  // the Board page's game at that ply
            page_      = Page::Board;
            board_.ply = std::clamp(std::atoi(w.c_str() + 10), 0, static_cast<int>(board_.game.moves.size()));
        } else if (w.rfind("board-3d=", 0) == 0) {  // the 3D board turned, YAW,PITCH in radians
            float yaw = 0.0f, pitch = 0.95f;
            std::sscanf(w.c_str() + 9, "%f,%f", &yaw, &pitch);
            board3d_.setView(yaw, pitch);
        } else if (w == "board-edit") {  // the position editor
            page_ = Page::Board;
            openPositionEditor();
        } else if (w == "board-play" || w == "board-play-start") {  // a game from the board: its settings; started
            page_             = Page::Board;
            board_.play_open  = true;
            if (w == "board-play-start") {
                loadBoardSettings();
                if (engines_.size() > 1) {
                    board_.settings.game_first  = engines_[0].name;
                    board_.settings.game_second = engines_[1].name;
                }
                startBoardGames();
            }
        } else if (w == "engines-console") {  // the console of the first engine, which answered uci
            page_            = Page::Engines;
            selected_engine_ = engines_.empty() ? -1 : 0;
            openEngineConsole(0);
        } else if (w == "engines-refresh") {
            page_ = Page::Engines;
            refreshEngineFolder();
        } else if (w.rfind("engines-import=", 0) == 0) {  // engines-import=FILE: the preview of that engine list
            page_ = Page::Engines;
            importEngines(importEngineList(w.substr(15)), {}, w.substr(15));
        } else if (w.rfind("engines-ratings", 0) == 0) {
            // engines-ratings=FILE: the preview of that rating list.
            page_ = Page::Engines;
            if (const auto eq = w.find('='); eq != std::string::npos)
                loadRatingList(w.substr(eq + 1));
            else
                openRatingImport();
        } else if (w == "console")
            page_ = Page::Log;
        else if (w == "settings")
            page_ = Page::Settings;
        else if (w == "appearance") {  // its section of Settings
            page_                 = Page::Settings;
            scroll_to_appearance_ = true;
        }
        else if (w == "machines" || w == "machines-test" || w == "machines-files") {
            // The first machine's card in Settings; tested, or the setup's books and tablebases compared with its.
            page_               = Page::Settings;
            scroll_to_machines_ = true;
            if (!machines_.empty()) {
                machine_open_ = machines_.front().id;
                if (w == "machines-test") startMachineJob(machine_open_, MachineJob::Kind::Test);
                if (w == "machines-files") startMachineJob(machine_open_, MachineJob::Kind::Files);
            }
        } else if (w.rfind("machines-copy=", 0) == 0) {  // machines-copy=ENGINE: its copy to the first machine offered
            if (!machines_.empty()) offerEngineCopy(machines_.front().id, w.substr(14));
        } else if (w == "ratings" || w == "ratings-engine" || w == "ratings-h2h" || w == "ratings-progress") {
            page_                = Page::Ratings;
            ratings_tab_request_ = w == "ratings-engine" ? 1 : w == "ratings-h2h" ? 2 : w == "ratings-progress" ? 3 : 0;
        } else if (w == "ratings-players") {  // the players of the PGN files and the library engines they are
            page_             = Page::Ratings;
            open_pgn_players_ = true;
        } else if (w == "books")
            page_ = Page::Books;
        else if (w.rfind("books-item=", 0) == 0) {  // books-item=N: the Nth opening of the first book on its board
            page_              = Page::Books;
            book_item_request_ = std::max(0, std::atoi(w.c_str() + 11) - 1);
        }
        else if (w == "books-create") {
            page_             = Page::Books;
            open_book_create_ = true;
        } else if (w.rfind("books-chess324=", 0) == 0) {  // books-chess324=FILE: the Chess324 book written there
            page_ = Page::Books;
            writeChess324Book(w.substr(15), false);
        } else if (w.rfind("books-build", 0) == 0) {
            // books-build[=PGN[>BIN]]: the form of Build a Polyglot book, with that PGN file, building BIN.
            page_ = Page::Books;
            openBookBuild();
            if (const auto eq = w.find('='); eq != std::string::npos) {
                const auto arg       = w.substr(eq + 1);
                const auto gt        = arg.find('>');
                book_build_form_.pgn = arg.substr(0, gt);
                if (gt != std::string::npos) startBookBuild(arg.substr(gt + 1));
            }
        } else if (w == "home")
            page_ = Page::Home;
        else if (w == "home-resume" || w == "setup-start") {
            // Resume or Start pressed on the first frame, while the engines that need it are read.
            page_ = w == "home-resume" ? Page::Home : Page::NewMatch;
            if (w == "setup-start")
                startRun();
            else if (unfinished_)
                resumeRun();
        } else if (w == "engines-test") {
            // The quick test of the first engine pressed on the first frame, while it is read.
            page_            = Page::Engines;
            selected_engine_ = engines_.empty() ? -1 : 0;
            if (!engines_.empty()) startQuickTest(0);
        } else if (w == "suite-then-start") {
            // The test suite started, then Start of the setup pressed while it runs.
            page_ = Page::NewMatch;
            loadSuiteSetup();
            startSuite();
            startRun();
        } else if (w == "suites")
            page_ = Page::Suites;
        else if (w == "guide" || w.rfind("guide=", 0) == 0) {
            // guide[=ID]: the guide panel at the section ID (the page's without one).
            guide_.open(w.size() > 6 ? w.substr(6) : std::string(guideSection()));
        } else if (w.rfind("guide-search=", 0) == 0) {  // guide-search=TEXT: the guide panel, searching TEXT
            guide_.search(w.substr(13));
        } else if (w.rfind("palette", 0) == 0) {  // palette=TEXT: the command palette, TEXT typed in it
            palette_request_ = true;
            if (const auto eq = w.find('='); eq != std::string::npos) palette_query_ = w.substr(eq + 1);
        } else if (w == "layout-menu" || w == "layout-save") {  // the Layout menu, or its Save the layout as…
            page_ = Page::Tournament;
            if (w == "layout-menu")
                open_layout_menu_ = true;
            else
                open_save_layout_ = true;
        } else if (w.rfind("show=", 0) == 0) {  // show=PANEL: as the palette's Show <panel> (live_board...)
            page_ = Page::Tournament;
            showPanel(w.substr(5));
        } else if (w.rfind("detach=", 0) == 0) {  // detach=N: the board of game slot N (from 1) detached
            page_ = Page::Tournament;
            showPanel(layout::detachedPanel(std::max(0, std::atoi(w.c_str() + 7) - 1)));
        } else if (w.rfind("layout-save=", 0) == 0) {  // layout-save=NAME: the layout shown, saved as NAME
            if (!saveLayoutAs(w.substr(12))) Console::instance().add("No name for the layout.", true);
        } else if (w.rfind("layout=", 0) == 0) {  // layout=KEY|NAME: a ready-made layout (tabs, follow...) or one saved
            if (!applyLayout(w.substr(7)))
                Console::instance().add(fmt::format("No layout named {}.", w.substr(7)), true);
        } else if (w.rfind("game-menu=", 0) == 0) {  // game-menu=N: the menu of game N in the Games tab
            page_           = Page::Tournament;
            tab_request_    = Tab::Games;
            open_game_menu_ = std::strtoull(w.c_str() + 10, nullptr, 10);
        } else if (w == "big-screen" || w.rfind("big-screen=", 0) == 0) {  // big-screen=N: the game of slot N (from 1)
            page_ = Page::Tournament;
            startBigScreen(w.size() > 11 ? std::max(0, std::atoi(w.c_str() + 11) - 1) : live_slot_);
        } else if (w.rfind("end-menu=", 0) == 0) {  // end-menu=N: "End with result" of the game of slot N (from 1)
            page_          = Page::Tournament;
            tab_request_   = Tab::Live;
            live_slot_     = std::max(0, std::atoi(w.c_str() + 9) - 1);
            open_end_menu_ = live_slot_ + 1;
        } else if (w == "submission-export") {  // the Export for a rating list dialog of the Games tab
            page_        = Page::Tournament;
            tab_request_ = Tab::Games;
            openSubmissionExport();
        } else if (w.rfind("live-back=", 0) == 0) {  // live-back=N: the game followed stepped back N plies
            page_        = Page::Tournament;
            tab_request_ = Tab::Live;
            if (live_slot_ >= 0 && live_slot_ < static_cast<int>(slot_views_.size())) {
                auto& v         = slot_views_[static_cast<std::size_t>(live_slot_)];
                const int total = static_cast<int>(v.state.moves.size());
                v.nav.show(total - std::atoi(w.c_str() + 10), total);
            }
        } else if (w.rfind("games-", 0) == 0 || w.rfind("live-", 0) == 0) {
            // The tab with a chart under the game: games-time, live-agreement...
            const auto chart                     = w.substr(w.find('-') + 1);
            const auto kind                      = chart == "time"        ? MoveChart::Time
                                                   : chart == "depth"     ? MoveChart::Depth
                                                   : chart == "nodes"     ? MoveChart::Nodes
                                                   : chart == "speed"     ? MoveChart::Speed
                                                   : chart == "tbhits"    ? MoveChart::TbHits
                                                   : chart == "agreement" ? MoveChart::Agreement
                                                                          : MoveChart::Eval;
            page_                                = Page::Tournament;
            const bool live                      = w[0] == 'l';
            tab_request_                         = live ? Tab::Live : Tab::Games;
            (live ? live_chart_ : replay_chart_) = kind;
        } else {
            // open=DIR, open-TAB=DIR: a tournament's folder opened as Open a folder… does, on that tab.
            std::string tab = w;
            if (w.rfind("open", 0) == 0 && w.find('=') != std::string::npos) {
                const auto eq = w.find('=');
                tab           = w.substr(std::min<std::size_t>(5, eq), eq - std::min<std::size_t>(5, eq));
                openTournament(w.substr(eq + 1));
            }
            page_        = Page::Tournament;
            tab_request_ = tab == "live"        ? Tab::Live
                           : tab == "games"     ? Tab::Games
                           : tab == "openings"  ? Tab::Openings
                           : tab == "standings" ? Tab::Standings
                           : tab == "schedule"  ? Tab::Schedule
                           : tab == "bracket"   ? Tab::Bracket
                           : tab == "broadcast" ? Tab::Broadcast
                                                : Tab::Overview;
        }
        focus_done_ = true;
    }

    // The big screen takes the main window's place: the panels taken out of it stay on screen.
    if (big_.on) {
        drawBigScreen();
    } else {
        drawShell();
    }
    drawPanels();
    drawSprtCalculator();
    drawQuitModal();
    drawImportPreview();
    drawRatingImport();
    drawPgnPlayers();
    drawPalette();

    // The window title tells how the run is going, also when the window is in the background.
    {
        std::string title = "fastchess-gui " FCG_VERSION;
        if (run_->running()) {
            const auto& info = run_->info();
            title = trf("{} – {} · {} games", run_->paused() ? TR("Paused") : TR("Running"), info.name, games_.size());
            if (info.sprt) title += fmt::format(" · LLR {:.2f}", current_.llr);
        } else if (run_->state() == Run::State::Finished) {
            title = trf("{} – fastchess-gui", TR("Finished")) + " " FCG_VERSION;
        }
        // The others playing, after it.
        if (const int others = runsPlaying() - (run_->running() ? 1 : 0); others > 0) {
            title += " " + trn("(+{} tournament)", "(+{} tournaments)", others, others);
        }
        if (title != window_title_) {
            SDL_SetWindowTitle(window_, title.c_str());
            window_title_ = title;
        }
    }

    // Persist the setup a moment after the last edit.
    const auto now = std::chrono::steady_clock::now();
    if (setup_dirty_ && now - last_setup_save_ > std::chrono::seconds(2)) {
        storage_.saveSetup(setup_);
        setup_dirty_     = false;
        last_setup_save_ = now;
    }
}

// ---------------------------------------------------------------------------
// Status

std::string App::runStatusText() const {
    if (run_->state() == Run::State::Idle) {
        return trf("Ready · {} cores, {} threads", topology_.physical, topology_.logical);
    }

    if (run_->state() == Run::State::Finished) return finish_message_;

    const auto& info = run_->info();
    std::string state;
    if (run_->stopping()) {
        state = TR("Stopping…");
    } else if (run_->paused()) {
        const int n = run_->runningGames();
        state =
            n > 0 ? trn("Paused ({} game finishing)", "Paused ({} games finishing)", n, n) : std::string(TR("Paused"));
        if (run_->stoppingWhenIdle()) state = trf("Stopping after the running games ({})", n);
    } else {
        state = TR("Running");
    }

    const auto played  = games_.size();
    const auto elapsed = std::chrono::duration<double>(Clock::now() - info.started).count();
    const auto rate    = games_per_hour_;  // refreshed once per second in pollRun()

    const auto total_text = info.total_games ? fmt::format("{}", info.total_games) : std::string("∞");
    // The pace once it is known: none rather than 0 in the first seconds.
    std::string text =
        rate > 0 ? trf("{} · {} / {} games · {} playing · {:.0f} games/h · {}", state, played, total_text,
                       run_->runningGames(), rate, ui::formatDuration(elapsed))
                 : trf("{} · {} / {} games · {} playing · {}", state, played, total_text, run_->runningGames(),
                       ui::formatDuration(elapsed));

    if (info.sprt && sprt_) {
        text +=
            fmt::format(" · LLR {:.2f} [{:.2f}, {:.2f}]", current_.llr, sprt_->getLowerBound(), sprt_->getUpperBound());
    }

    return text;
}

void App::drawQuitModal() {
    if (open_quit_modal_) {
        ImGui::OpenPopup(TR("Quit?###quit"));
        open_quit_modal_ = false;
    }

    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(TR("Quit?###quit"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // The tournaments play on in their own processes, unless asked to stop; the quick test plays here, and stops.
        const int playing = runsPlaying();
        const bool keep   = std::any_of(runs_.begin(), runs_.end(),
                                        [](const OpenRun& r) { return r.run->running() && r.run->remote(); });
        if (keep) {
            ImGui::TextUnformatted(
                trn("{} tournament is running. It can play on without the window, which shows it again when it opens.",
                    "{} tournaments are running. They can play on without the window, which shows them again when it "
                    "opens.",
                    playing, playing)
                    .c_str());
        } else {
            ImGui::TextUnformatted(TR("A match is running. The games in progress will be lost."));
        }
        ImGui::Spacing();
        if (keep) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
            const bool chosen = ImGui::Button(TR("Keep them running and quit"));
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", TR("Each plays on in its own process. The window shows them again at its next "
                                           "launch; fastchess-gui --attach <folder> talks with one from a terminal."));
            }
            if (chosen || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                keepRunningAndQuit();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if (ImGui::Button(TR("Stop and quit"))) {
            forceQuit();
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("The games in progress are lost; each tournament resumes exactly where it "
                                       "stopped."));
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (quit_pending_ && runsPlaying() > 0) {
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::Begin(
            "##stopping", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted(TR("Stopping the engines…"));
        ImGui::End();
    }
}

// ---------------------------------------------------------------------------
// Run lifecycle

void App::startRun() {
    storage_.saveSetup(setup_);
    if (startAfterReads("start", enginesOfSetup(setup_), pgnBookNeeds(setup_), [this] { startRun(); })) return;

    // On a remote machine (Run on): its processors make the concurrency, its memory is its own.
    const Machine* machine = setup_.machine.empty() ? nullptr : machineById(setup_.machine);
    CpuTopology topology   = topology_;
    if (machine && machine->system.threads > 0) {
        topology.logical  = machine->system.threads;
        topology.physical = std::max(1, machine->system.threads / 2);
    }

    // Hash tables larger than the memory installed cannot work; larger than the free memory, they swap.
    if (const auto memory = setupMemory(setup_, engines_, topology_); !machine && memory.aboveInstalled()) {
        start_error_ = memoryNotice(setup_, engines_, memory).text;
        Console::instance().add(trf("Cannot start the match: {}", start_error_), true);
        setStartFixes();
        return;
    } else if (!machine && memory.aboveAvailable()) {
        Console::instance().add(memoryNotice(setup_, engines_, memory).text, true);
    }

    // The tournament's folder, made here (what the setup holds is checked here, its errors said here), then played
    // by a process of its own.
    const auto dir = setup_.format == TournamentFormat::Match
                         ? storage_.runsDir() / makeRunDirName(setup_.engine_a.empty() ? "A" : setup_.engine_a,
                                                               setup_.engine_b.empty() ? "B" : setup_.engine_b)
                         : storage_.runsDir() /
                               makeTournamentDirName(setup_.name.empty() ? defaultTournamentName(setup_) : setup_.name);
    try {
        const auto built = buildRun(setup_, engines_, topology, dir);
        (void)tour::TournamentState::create(dir, built.spec);  // closed at once: the process opens it
    } catch (const std::exception& e) {
        start_error_ = e.what();
    }
    if (start_error_.empty()) {
        start_error_ = setup_.machine.empty() ? launchTournament(dir) : launchOnMachine(dir, setup_.machine);
    }
    if (!start_error_.empty()) {
        Console::instance().add(trf("Cannot start the match: {}", start_error_), true);
        setStartFixes();
        return;
    }

    user_stop_       = false;
    resume_guarded_  = false;
    const auto& info = run_->info();
    storage_.setRunningMarker(RunningMarker{runMarkerDir(), false});
    Console::instance().add(trf("Started: {}, {}, {} concurrent games{}. Folder: {}", info.name, info.limit_text,
                                info.concurrency, info.affinity ? TR(", CPU affinity") : "", info.dir.string()));
}

void App::resumeRun(bool at_launch) {
    if (!unfinished_) return;
    // It plays the engines copied in its folder, so no read of the library is waited for. Put off all the same (see
    // startGate): still the same tournament when it starts, not one opened meanwhile. It waits for its PGN books,
    // made again if their files changed since.
    auto books = pgnBookNeeds(unfinished_->spec);
    if (startAfterReads(resumeKey(unfinished_->dir), {}, std::move(books), [this, at_launch, dir = unfinished_->dir] {
            if (unfinished_ && unfinished_->dir == dir) resumeRun(at_launch);
        })) {
        return;
    }

    // Resumed at launch: the marker says so before anything can end the program (RunningMarker::resuming).
    const auto dir = fs::absolute(unfinished_->dir).lexically_normal().string();
    if (at_launch) storage_.setRunningMarker(RunningMarker{dir, false, true});
    start_error_ = resumeCheck(unfinished_->dir);
    if (start_error_.empty()) {
        // A tournament of a remote machine resumes there.
        const auto place = readMachinePlace(unfinished_->dir);
        start_error_ = place ? launchOnMachine(unfinished_->dir, place->machine) : launchTournament(unfinished_->dir);
    }
    if (!start_error_.empty()) {
        // It did not start, which ended nothing: it is tried again at the next launch.
        if (at_launch) storage_.setRunningMarker(RunningMarker{dir, false, false});
        Console::instance().add(start_error_, true);
        return;
    }

    user_stop_      = false;
    resume_guarded_ = at_launch;
    // The games that count, as games_ holds: voided games are not shown.
    if (const auto* t = run_->tournament()) {
        const auto results = t->results();
        games_at_start_    = static_cast<std::size_t>(
            std::count_if(results.begin(), results.end(), [](const auto& r) { return !r.voided; }));
        // A program rebuilt or gone since its engine joined: said in the header, with Continue and Pause, not only
        // in the Log. It does not stop by itself.
        resumed_programs_ = changedPrograms(t->spec());
    }
    const auto& info = run_->info();
    storage_.setRunningMarker(RunningMarker{runMarkerDir(), false, at_launch});
    if (at_launch) Console::instance().add(trf("{} was running when the program ended: it resumes.", info.name));
    Console::instance().add(
        trf("Resumed: {}, {} games already played. Folder: {}", info.name, games_at_start_, info.dir.string()));
}

void App::resumeAtLaunch() {
    const auto markers = storage_.runningMarkers();
    bool resumed = false, queue = false;
    for (const auto& marker : markers) {
        // Played on without the window (it was let go when it closed): shown again, as it stands.
        if (attachTournament(marker.dir, marker.weight)) {
            resumed = true;
            continue;
        }
        const auto summary = tour::summarize(marker.dir);
        const bool unfinished =
            summary && !summary->finished && !(summary->total && summary->played >= *summary->total);
        // Another window of the profile plays it, and the marker is that window's: its folder is locked.
        bool busy = false;
        std::error_code ec;
        if (fs::is_directory(marker.dir, ec)) {
            try {
                const tour::FolderLock probe(marker.dir);
            } catch (const tour::FolderBusy&) {
                busy = true;
            } catch (const std::exception&) {
            }
        }
        const auto name = summary ? summary->spec.name : marker.dir;
        switch (launchResume(setup_.resume_at_launch, options_.autostart, marker, unfinished, queue_.empty(), busy)) {
            case LaunchResume::Resume:
                // The marker stays until the tournament ends: a resume that cannot start is tried again at the next
                // launch. resumeRun() says that it resumes once it does.
                unfinished_ = summary;
                resumeRun(true);
                if (const int i = findRun(marker.dir); i >= 0)
                    runs_[static_cast<std::size_t>(i)].weight = marker.weight;
                resumed = true;
                break;
            case LaunchResume::Busy:
                Console::instance().add(trf("{} is playing in another window: it is not resumed here.", name));
                break;
            case LaunchResume::Crashed:
                // Resuming it again could end the program again, at every launch: the user decides.
                storage_.clearRunningMarker(marker.dir);
                unfinished_     = summary;
                resume_crashed_ = fs::absolute(summary->dir).lexically_normal();
                Console::instance().add(
                    trf("The program ended while it resumed {} at the last launch: it is not resumed "
                        "automatically again. Resume it from the Tournaments page.",
                        name),
                    true);
                break;
            case LaunchResume::Queue:
                storage_.clearRunningMarker(marker.dir);
                queue = true;
                break;
            case LaunchResume::Nothing:
                // Paused, or not to be resumed at launch: it waits for the user, as any interrupted tournament.
                storage_.clearRunningMarker(marker.dir);
                break;
        }
    }
    if (queue && !resumed) {
        Console::instance().add(TR("The tournament running when the program ended is over: the queue goes on."));
        startFromQueue(true);
    }
}

void App::setRunPaused(bool paused) {
    run_->setPaused(paused);
    const auto* open = shownRun();
    if (open && !open->quick_test && !user_stop_)
        storage_.setRunningMarker(RunningMarker{runMarkerDir(), paused, resume_guarded_, open->weight});
}

void App::stopRun(bool now) {
    if (!run_->running()) return;
    user_stop_ = true;
    if (now) {
        run_->stopNow();
    } else {
        run_->stopWhenIdle();
    }
    // Stopped, not interrupted: not resumed at the next launch, even if the program ends before the engines stop.
    storage_.clearRunningMarker(runMarkerDir());
}

std::string App::runMarkerDir() const { return fs::absolute(run_->info().dir).lexically_normal().string(); }

void App::resetRunView() {
    page_ = Page::Tournament;
    // On its overview, when the panels are tabs of one node; side by side, they stay as they are.
    if (const auto* root = ImGui::DockBuilderGetNode(workspaceDock());
        panelOpen(layout::panel::OVERVIEW) && (!root || !root->IsSplitNode()))
        tab_request_ = Tab::Overview;
    tournaments_dirty_ = true;
    unfinished_.reset();
    resumed_programs_.clear();
    finish_message_.clear();
    console_mark_   = Console::instance().added();  // what fastchess says from now on is this tournament's
    games_at_start_ = 0;
    clearRunView();
}

void App::clearRunView() {
    select_game_id_.reset();
    games_.clear();
    history_.clear();
    stats_   = {};
    current_ = {};
    reasons_.clear();
    openings_.clear();
    by_opening_.clear();
    by_opening_for_   = ~std::size_t(0);
    schedule_         = {};
    edit_preview_     = {};
    selected_opening_ = -1;
    totals_.clear();
    table_     = {};
    shown_     = {};
    table_for_ = ~std::size_t(0);
    anchor_.reset();
    crosstable_cell_.reset();
    standings_engine_.reset();
    submission_facts_for_ = ~uint64_t(0);
    standings_view_ = -1;
    games_subject_ = -1;
    filtered_.clear();
    filtered_for_  = ~std::size_t(0);
    selected_game_ = -1;
    replay_.reset();
    slot_views_.clear();
    // The detached boards were those of the games of the tournament before.
    for (auto it = panels_open_.begin(); it != panels_open_.end();) {
        if (layout::detachedSlot(*it)) {
            it = panels_open_.erase(it);
            panelsChanged();
        } else {
            ++it;
        }
    }

    const auto& info = run_->info();
    sprt_.reset();
    if (info.sprt) sprt_.emplace(info.alpha, info.beta, info.elo0, info.elo1, info.model, true);
    // The openings it replaced before it was opened are in its results already.
    openings_replaced_seen_ = run_->tournament() ? run_->tournament()->openingsReplaced() : 0;

    live_slot_        = std::clamp(live_slot_, 0, std::max(1, info.concurrency) - 1);
    games_per_hour_   = 0;
    last_rate_update_ = {};
}

void App::pollRun() {
    // Every tournament open: the shown one feeds the views below, the others keep their own observers.
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        runs_[i].run->update();
        if (i != shown_run_) watchStartFailures(runs_[i]);
    }
    // Its copy opened again (an edit undone in its process): the views start over from it.
    if (run_->takeReopened()) refreshAfterEdit();

    if (auto* obs = run_->observer()) {
        std::vector<fastchess::Stats> fresh;
        const auto before = games_.size();
        obs->drain(games_, fresh);

        for (const auto& s : fresh) {
            stats_ = s;
            history_.push_back(computeStats(s, run_->info().penta, sprt_ ? &*sprt_ : nullptr));
        }
        if (!fresh.empty()) current_ = history_.back();

        const auto now = std::chrono::steady_clock::now();
        if (games_.size() != before || now - last_totals_ > std::chrono::milliseconds(500)) {
            if (games_.size() != before) {
                reasons_  = obs->reasons();
                openings_ = obs->openings();
            }
            // A game asked for from the Ratings page, once its tournament's games are there.
            if (select_game_id_) {
                const auto it = std::find_if(games_.begin(), games_.end(),
                                             [&](const GameRecord& g) { return g.game_id == *select_game_id_; });
                if (it != games_.end()) {
                    selectGame(static_cast<int>(it - games_.begin()));
                    select_game_id_.reset();
                }
            }
            totals_      = obs->totals();
            last_totals_ = now;
        }
    }

    // Games per hour, recomputed at most once per second so the number does not flicker.
    if (run_->running()) {
        const auto now = Clock::now();
        if (now - last_rate_update_ >= std::chrono::seconds(1)) {
            const auto* t = run_->tournament();
            if (run_->remote() && t) {
                // Played elsewhere, perhaps long before this window connected: from the games' end times, those of the
                // last ten minutes (or since its first game), rather than from what this window was told.
                const auto wall = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();
                games_per_hour_ = home::gamesPerHour(t->results(), wall);
            } else {
                const auto elapsed = std::chrono::duration<double>(now - run_->info().started).count();
                // games_ can get below games_at_start_ when games are voided meanwhile.
                const auto played = games_.size() > games_at_start_ ? games_.size() - games_at_start_ : 0;
                games_per_hour_   = elapsed > 5 ? static_cast<double>(played) / (elapsed / 3600.0) : 0.0;
            }
            last_rate_update_ = now;
        }
    }

    // A resume at launch that has played a while did not end the program: the next launch may resume it again.
    if (resume_guarded_ && run_->running() && Clock::now() - run_->info().started >= std::chrono::seconds(30)) {
        resume_guarded_ = false;
        if (auto marker = storage_.runningMarker(runMarkerDir())) {
            marker->resuming = false;
            storage_.setRunningMarker(*marker);
        }
    }

    // A pair voided and played again from another opening, its own found unbalanced: the views show the results
    // as they count now, as after a void of the user's.
    if (const auto* t = run_->tournament()) {
        if (const auto replaced = t->openingsReplaced(); replaced != openings_replaced_seen_) {
            if (replaced > openings_replaced_seen_) {
                const auto n = replaced - openings_replaced_seen_;
                Console::instance().add(trn("{} unbalanced opening: its game pair is played again from another one.",
                                            "{} unbalanced openings: their game pairs are played again from others.", n,
                                            n));
            }
            openings_replaced_seen_ = replaced;
            refreshAfterEdit();
        }
    }

    if (auto* open = shownRun()) watchStartFailures(*open);
    arbitrate();
    arbitrateMachines();
    pollMachineRuns();
    // The queue's next tournament beside those playing, once cores are free (checked now and then).
    if (budget_setting_.queue_when_free && !queue_.empty() && !pending_start_ && !quit_pending_ && runsPlaying() > 0 &&
        !queue_held_ && std::chrono::steady_clock::now() - queue_checked_ > std::chrono::seconds(2)) {
        queue_checked_ = std::chrono::steady_clock::now();
        if (queueBeside(0)) startFromQueue(false);
    }
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        if (!runs_[i].run->takeFinishedEvent()) continue;
        if (i == shown_run_) {
            onRunFinished();
        } else {
            onBackgroundRunFinished(i);
        }
    }

    if (quit_pending_ && runsPlaying() == 0) quit_ = true;
}

void App::watchStartFailures(OpenRun& open) {
    const auto* t = open.run->tournament();
    if (!t || open.quick_test) return;
    auto& start_failures_seen_ = open.start_failures_seen;
    auto& start_failures_told_ = open.start_failures_told;
    const auto& failures       = t->startFailures();
    if (failures.version() == start_failures_seen_) return;
    start_failures_seen_ = failures.version();

    std::set<int> failed, failing;
    for (const auto& e : failures.engines()) {
        const auto* p = t->spec().participant(e.participant);
        if (!p || !p->active) continue;
        failing.insert(e.participant);
        if (!e.failed) continue;
        failed.insert(e.participant);
        if (start_failures_told_.count(e.participant)) continue;
        // A tournament that stops for it says so at its end; otherwise the night goes on, and the tester is told.
        if (t->spec().stop_on_start_failure) continue;
        const auto text = trf("{} cannot start: {}. Its games are set aside, the others go on.", e.name, e.error);
        Console::instance().add(text, true);
        if (setup_.notify) notifyRunFinished(window_, t->spec().name, text);
    }
    for (const int id : start_failures_told_) {
        const auto* p = t->spec().participant(id);
        if (p && p->active && !failing.count(id))
            Console::instance().add(trf("{} started again: its games are played again.", p->engine.name));
    }
    start_failures_told_ = std::move(failed);
}

void App::onRunFinished() {
    if (const auto* open = shownRun(); open && open->quick_test) {
        finishQuickTest();
        return;
    }
    // Another window shows it: said, and left to it (its marker is that window's too).
    if (run_->elsewhere()) {
        finish_message_ = run_->error();
        Console::instance().add(finish_message_, true);
        return;
    }
    const auto& info  = run_->info();
    const auto error  = run_->error();
    const auto played = stats_.sum();

    // Quitting while it runs leaves it to be resumed at the next launch; otherwise it ended, or the user stopped it.
    // Only this window's marker: another window of the profile may have written its own since.
    const auto marker_dir = runMarkerDir();
    if (!keepMarkerAtEnd(quit_pending_, user_stop_)) {
        storage_.clearRunningMarker(marker_dir);
    } else if (auto marker = storage_.runningMarker(marker_dir); marker && marker->resuming) {
        marker->resuming = false;  // quit by the user: the resume did not end the program
        storage_.setRunningMarker(*marker);
    }
    resume_guarded_ = false;

    // The engine that stopped it: one that could not start (fastchess said so in the log), or that crashed.
    stopped_by_.clear();
    std::string why;
    const bool abnormal = run_->abnormal();
    if (error.empty() && abnormal && !run_->stoppedByUser()) {
        // Its process said which engine; a tournament played here, the window's log does.
        auto failed = run_->startupFailed();
        if (!failed && !run_->remote()) failed = startupFailure(Console::instance().since(console_mark_));
        if (failed) {
            stopped_by_ = failed->engine;
            why         = trf("Interrupted: {} could not start: {}", failed->engine, failed->reason);
        } else if (const auto* t = run_->tournament()) {
            const auto results = t->results();
            for (auto r = results.rbegin(); r != results.rend(); ++r) {
                using T = fastchess::MatchTermination;
                if (r->voided || (r->termination != T::DISCONNECT && r->termination != T::STALL)) continue;
                const int loser = r->scoreOf(r->white) == 0 ? r->white : r->black;
                if (const auto* p = t->spec().participant(loser)) stopped_by_ = p->engine.name;
                if (!stopped_by_.empty())
                    why = trf("Interrupted: {} crashed or stopped responding (game {})", stopped_by_, r->number() + 1);
                break;
            }
        }
    }

    std::string verdict;
    const auto* stalled = run_->tournament();
    if (stalled && !stalled->startFailures().stalled()) stalled = nullptr;
    if (!error.empty()) {
        verdict = trf("Error: {}", error);
    } else if (!why.empty()) {
        verdict = why;
    } else if (abnormal && !run_->stoppedByUser()) {
        verdict = TR("Interrupted: an engine crashed or stopped responding (see the Log)");
    } else if (stalled && !run_->stoppedByUser()) {
        std::string names;
        for (const auto& e : stalled->startFailures().engines()) {
            const auto* p = stalled->spec().participant(e.participant);
            if (p && p->active) names += (names.empty() ? "" : ", ") + e.name;
        }
        verdict = trf("Stopped: no game is left to play without {}, which cannot start", names);
    } else if (info.sprt && sprt_ && sprtVerdict() == fastchess::SPRT_H1) {
        verdict = trf("SPRT finished: H1 accepted, {} is stronger", info.engine_a);
    } else if (info.sprt && sprt_ && sprtVerdict() == fastchess::SPRT_H0) {
        verdict = trf("SPRT finished: H0 accepted, {} is not stronger", info.engine_a);
    } else if (run_->stoppedByUser()) {
        verdict = TR("Stopped");
    } else {
        verdict = TR("Finished");
    }

    const auto duration = ui::formatDuration(std::chrono::duration<double>(info.finished - info.started).count());
    if (isMatch()) {
        finish_message_ = trf("{} · {} games · Elo {} · {}", verdict, played,
                              current_.valid ? formatElo(current_.elo, current_.elo_err) : std::string("—"), duration);
        if (info.sprt) finish_message_ += fmt::format(" · LLR {:.2f}", current_.llr);
    } else {
        table_for_ = ~std::size_t(0);  // with the last games, whatever a big table's pace
        refreshTable();
        auto leader = table_.rows.empty() ? std::string("—") : table_.rows.front().name;
        // A knockout is won in its final, not on points.
        if (const auto* t = run_->tournament(); t && t->spec().format == tour::Format::Knockout && t->finished()) {
            const auto brackets = t->brackets();
            if (!brackets.empty() && brackets.rbegin()->second.size() == 1 &&
                brackets.rbegin()->second[0].size() == 2) {
                const auto& final_pair = brackets.rbegin()->second[0];
                const double a         = table_.pair(final_pair[0], final_pair[1]).points();
                const double b         = table_.pair(final_pair[1], final_pair[0]).points();
                if (const auto* p = t->spec().participant(b > a ? final_pair[1] : final_pair[0]))
                    leader = p->engine.name;
            }
        }
        finish_message_ = trf("{} · {} games · first: {} · {}", verdict, games_.size(), leader, duration);
    }

    Console::instance().add(finish_message_, !error.empty() || !why.empty());
    if (!error.empty() || abnormal) setFinishFixes();

    // A stopped or interrupted match can be continued later.
    unfinished_        = unfinishedMatch(storage_.runsDir());
    tournaments_dirty_ = true;

    if (setup_.notify) {
        notifyRunFinished(window_, info.name, finish_message_);
    }

    // The queue goes on after a tournament that ended by itself, not after a stop or an error.
    const bool completed = error.empty() && !run_->stoppedByUser() && !abnormal;
    if (completed && !queue_.empty() && !quit_pending_) {
        // Held by Pause all: Resume all starts it.
        if (queue_held_) {
            queue_blocked_ = true;
            Console::instance().add(TR("The queue waits for Resume all."));
        } else {
            startFromQueue(true);
        }
    }
}

void App::enqueue() {
    QueuedTournament item;
    item.setup = setup_;
    item.label = setup_.name.empty() ? defaultTournamentName(setup_) : setup_.name;
    queue_.push_back(item);
    storage_.saveQueue(queue_);
    Console::instance().add(trf("Queued: {}", item.label));
}

void App::startFromQueue(bool go_on, std::size_t from, const std::string& label) {
    // One after the other: the next starts when no tournament plays (its process ended), or beside them when the
    // budget has cores they cannot take (Start when cores are free).
    if (!queueBeside(label.empty() ? 0 : from)) {
        for (const auto& r : runs_) {
            if (r.run->running() && !r.quick_test) return;
        }
    }
    // Going on from the tournament `label` (put off, or the next after one that could not start), unless the queue
    // changed meanwhile.
    if (!label.empty() && (from >= queue_.size() || queue_[from].label != label)) from = 0;
    for (std::size_t i = from; i < queue_.size(); ++i) {
        // Put off while an engine of this tournament is read, not one of the tournaments after it: a start put off
        // is not one that failed. When that read fails, it stays queued, and the queue goes on with the next one.
        const auto here = queue_[i].label;
        const auto next = queueNextAfterFailure(i, queue_.size(), go_on);
        if (startAfterReads(
                "queue", queueNeeds(queue_, i), pgnBookNeeds(queue_[i].setup),
                [this, go_on, i, here] { startFromQueue(go_on, i, here); },
                next ? std::function<void()>(
                           [this, n = *next, there = queue_[*next].label] { startFromQueue(true, n, there); })
                     : std::function<void()>())) {
            return;
        }
        auto setup = queue_[i].setup;
        // The look of the program follows the settings, not the queued setup.
        keepProgramSettings(setup, setup_);
        setup_ = std::move(setup);
        startRun();
        // Taken off the queue once started: one that cannot start (an engine removed since...) is not lost.
        if (start_error_.empty() && run_->running()) {
            queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(i));
            storage_.saveQueue(queue_);
            return;
        }
        if (!go_on) return;
    }
}

fastchess::SPRTResult App::sprtVerdict() const {
    const auto* t  = run_->tournament();
    const auto why = t ? t->finishReason() : std::string();
    return why == "sprt_h1" ? fastchess::SPRT_H1 : why == "sprt_h0" ? fastchess::SPRT_H0 : fastchess::SPRT_CONTINUE;
}

std::string App::summaryText() const {
    const auto& info = run_->info();
    const auto& s    = stats_;

    std::string text = fmt::format("{} vs {}, {}, {} games\n", info.engine_a, info.engine_b, info.limit_text, s.sum());
    text += fmt::format("Elo: {:.2f} +/- {:.2f}, nElo: {:.2f} +/- {:.2f}\n", current_.elo, current_.elo_err,
                        current_.nelo, current_.nelo_err);
    text +=
        fmt::format("LOS: {:.2f} %, DrawRatio: {:.2f} %\n", current_.los, s.sum() ? 100.0 * s.draws / s.sum() : 0.0);
    text += fmt::format("Games: {}, Wins: {}, Losses: {}, Draws: {}, Points: {:.1f} ({:.2f} %)\n", s.sum(), s.wins,
                        s.losses, s.draws, s.points(), s.sum() ? s.pointsRatio() : 0.0);
    if (info.penta) {
        text += fmt::format("Ptnml(0-2): [{}, {}, {}, {}, {}]\n", s.penta_LL, s.penta_LD, s.penta_WL + s.penta_DD,
                            s.penta_WD, s.penta_WW);
    }
    if (info.sprt && sprt_) {
        text += fmt::format("LLR: {:.2f} ({:.2f}, {:.2f}) [{:.2f}, {:.2f}] {}\n", current_.llr, sprt_->getLowerBound(),
                            sprt_->getUpperBound(), info.elo0, info.elo1, info.model);
        // The bounds above are those the SPRT was given: the tester's, the bias on them.
        if (info.sprt_bias != 0 || info.sprt_min_games > 0) {
            text += tour::sprtText(info.elo0 - info.sprt_bias, info.elo1 - info.sprt_bias, info.sprt_bias,
                                   info.sprt_min_games) +
                    "\n";
        }
    }
    if (s.sum() > 0 && info.penta) text += statisticsText(matchStatistics(s, sprt_ ? &*sprt_ : nullptr));
    return text;
}

// ---------------------------------------------------------------------------
// Dialog answers

void App::handleDialogs() {
    while (auto answer = dialogs_.take()) {
        const auto& path = answer->path;

        switch (answer->purpose) {
            case Dialogs::Purpose::AddEngine:
                addEngine(path);
                break;
            case Dialogs::Purpose::TournamentsFolder:
                storage_.setRunsDir(path);
                tournaments_dirty_ = true;
                game_db_dirty_     = true;
                unfinished_        = unfinishedMatch(storage_.runsDir());
                break;
            case Dialogs::Purpose::ExportRatings: {
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out << tour::ratingListCsv(rating_list_);
                Console::instance().add(out ? trf("Rating list written to {}", path) : trf("Cannot write {}", path),
                                        !out);
                break;
            }
            case Dialogs::Purpose::ExportRatingHtml:
            case Dialogs::Purpose::ExportRatingMarkdown: {
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out << (answer->purpose == Dialogs::Purpose::ExportRatingHtml
                            ? tour::ratingListHtml(rating_list_, ratingInfo())
                            : tour::ratingListMarkdown(rating_list_, ratingInfo()));
                Console::instance().add(out ? trf("Rating list written to {}", path) : trf("Cannot write {}", path),
                                        !out);
                break;
            }
            case Dialogs::Purpose::ExportRatingPgn: {
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out << tour::ratingPgn(*game_db_, ratingFilter());
                Console::instance().add(out ? trf("Games written to {}", path) : trf("Cannot write {}", path), !out);
                break;
            }
            case Dialogs::Purpose::ImportRatingPgn: {
                auto list = ratingPgns();
                if (std::find(list.begin(), list.end(), std::filesystem::path(path)) == list.end())
                    list.emplace_back(path);
                setRatingPgns(list);
                review_pgn_players_ = 1;
                break;
            }
            case Dialogs::Purpose::ExportHeadToHead: {
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                std::size_t n = 0;
                for (const auto i : h2h_.games) {
                    const auto& g   = game_db_->games[i];
                    const auto& src = game_db_->sources[g.source];
                    if (src.external) continue;
                    if (const auto text = tour::readSpan(src.pgn, g.pgn)) {
                        // A result set by hand, as it counts.
                        const auto set = game_db_->set_results.find(i);
                        out << (set == game_db_->set_results.end()
                                    ? *text
                                    : tour::withResult(*text, set->second.first, tour::OPERATOR_TERMINATION,
                                                       set->second.second));
                        ++n;
                    }
                }
                Console::instance().add(out ? trn("{} game exported to {}", "{} games exported to {}", n, n, path)
                                            : trf("Cannot write {}", path),
                                        !out);
                break;
            }
            case Dialogs::Purpose::ExportOpenings: {
                const auto shown = shownOpenings();
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                const auto epd = tour::openingsEpd(shown, export_balanced_only_);
                out << epd;
                const auto n = static_cast<std::size_t>(std::count(epd.begin(), epd.end(), '\n'));
                Console::instance().add(out ? trn("{} opening written to {}", "{} openings written to {}", n, n, path)
                                            : trf("Cannot write {}", path),
                                        !out);
                break;
            }
            case Dialogs::Purpose::AddBook:
                addBook(path);
                break;
            case Dialogs::Purpose::BookFile:
                if (selected_book_ >= 0 && selected_book_ < static_cast<int>(books_.size())) {
                    books_[selected_book_].path = path;
                    storage_.saveBooks(books_);
                    startBookScan(selected_book_);
                }
                break;
            case Dialogs::Purpose::Chess324Book:
            case Dialogs::Purpose::Chess324SymmetricalBook:
                writeChess324Book(path, answer->purpose == Dialogs::Purpose::Chess324SymmetricalBook);
                break;
            case Dialogs::Purpose::BuildBookPgn:
                book_build_form_.pgn = path;
                book_build_shown_    = true;
                break;
            case Dialogs::Purpose::BuildBookBin:
                startBookBuild(path);
                break;
            case Dialogs::Purpose::SuiteFile:
                openSuite(path);
                break;
            case Dialogs::Purpose::ExportSuiteCsv:
                exportSuiteCsv(path);
                break;
            case Dialogs::Purpose::SaveCleanBook:
                if (book_scan_) {
                    uint8_t drop = tour::BOOK_ILLEGAL | tour::BOOK_DUPLICATE | tour::BOOK_OVER;
                    if (drop_mirrored_) drop |= tour::BOOK_MIRRORED;
                    try {
                        const auto n = book_scan_->writeEpd(path, drop);
                        Console::instance().add(
                            trn("{} position written to {}", "{} positions written to {}", n, n, path));
                        addBook(path);
                    } catch (const std::exception& e) {
                        Console::instance().add(e.what(), true);
                    }
                }
                break;
            case Dialogs::Purpose::ImportEngineList:
                importEngines(importEngineList(path), {}, path);
                break;
            case Dialogs::Purpose::ImportEngineFolder:
                // Remembered, for Refresh to look in again.
                storage_.setEngineFolder(path);
                engine_folder_ = std::filesystem::path(path);
                importEngines(findEngines(path));
                break;
            case Dialogs::Purpose::ImportRatingList:
                loadRatingList(path);
                break;
            case Dialogs::Purpose::OpenTournament:
                openTournament(path);
                break;
            case Dialogs::Purpose::ExportStandings:
                if (const auto* t = run_->tournament()) {
                    std::ofstream out(path, std::ios::binary | std::ios::trunc);
                    const auto footer = tour::footerLine(t->spec(), shown_, t->totalGames());
                    out << (t->spec().format == tour::Format::TeamMatch
                                ? tour::teamHtml(teams_, table_, t->spec().name, footer)
                                : tour::tableHtml(shown_, t->spec().name, footer));
                    Console::instance().add(out ? trf("Standings written to {}", path) : trf("Cannot write {}", path),
                                            !out);
                }
                break;
            case Dialogs::Purpose::EngineCommand:
                if (selected_engine_ >= 0 && selected_engine_ < static_cast<int>(engines_.size())) {
                    engines_[selected_engine_].cmd = path;
                    refreshProgram(engines_[selected_engine_]);
                    storage_.saveEngines(engines_);
                    startProbe(selected_engine_);
                }
                break;
            case Dialogs::Purpose::EngineDir:
                if (selected_engine_ >= 0 && selected_engine_ < static_cast<int>(engines_.size())) {
                    engines_[selected_engine_].dir = path;
                    refreshProgram(engines_[selected_engine_]);
                    storage_.saveEngines(engines_);
                }
                break;
            case Dialogs::Purpose::EngineLogo:
                setEngineLogo(selected_engine_, path);
                break;
            case Dialogs::Purpose::PieceFolder: {
                auto settings         = appearance::current();
                settings.pieces       = appearance::FOLDER;
                settings.piece_folder = path;
                applyAppearance(settings);
                break;
            }
            case Dialogs::Purpose::EngineOption:
                if (selected_engine_ >= 0 && selected_engine_ < static_cast<int>(engines_.size())) {
                    engines_[selected_engine_].setOption(option_browse_, path);
                    storage_.saveEngines(engines_);
                }
                break;
            case Dialogs::Purpose::Book:
                setup_.book = path;
                saveSetupSoon();
                break;
            case Dialogs::Purpose::PgnOut:
                setup_.pgn_out = path;
                saveSetupSoon();
                break;
            case Dialogs::Purpose::Tablebases: {
#ifdef _WIN32
                const char separator = ';';
#else
                const char separator = ':';
#endif
                if (!setup_.tb_paths.empty()) setup_.tb_paths += separator;
                setup_.tb_paths += path;
                saveSetupSoon();
                break;
            }
            case Dialogs::Purpose::TablebasesFolder:
                setup_.tb_paths = path;
                saveSetupSoon();
                break;
            case Dialogs::Purpose::ExportGames:
                exportGames(path, false);
                break;
            case Dialogs::Purpose::ExportAll:
                exportGames(path, true);
                break;
            case Dialogs::Purpose::ExportSubmission:
                writeSubmissionExport(path);
                break;
            case Dialogs::Purpose::ConsoleLog:
                saveConsoleLog(path);
                break;
            case Dialogs::Purpose::BoardPgn:
                openBoardPgn(std::filesystem::path(path));
                break;
            case Dialogs::Purpose::MachineKey:
            case Dialogs::Purpose::MachineProgram:
                if (auto* machine = machineById(machine_dialog_for_)) {
                    (answer->purpose == Dialogs::Purpose::MachineKey ? machine->key_file : machine->program) = path;
                    saveMachines();
                }
                break;
            case Dialogs::Purpose::MachineEngineBuild:
                offerEngineCopy(machine_dialog_for_, machine_dialog_engine_, std::filesystem::path(path));
                break;
        }
    }
}

}  // namespace fcg
