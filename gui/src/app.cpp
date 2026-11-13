#include "app.hpp"

#include <algorithm>
#include <fstream>

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <fmt/include/fmt/format.h>
#include <core/globals/globals.hpp>
#include <tour/files.hpp>

#include "i18n.hpp"
#include "memory.hpp"
#include "notify.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

App::App(SDL_Window* window, Storage storage, CpuTopology topology, AppOptions options)
    : window_(window),
      storage_(std::move(storage)),
      topology_(topology),
      options_(std::move(options)),
      dialogs_(window) {
    engines_             = storage_.loadEngines();
    books_               = storage_.loadBooks();
    lichess_token_       = storage_.lichessToken();
    lichess_token_saved_ = lichess_token_;
    if (!books_.empty()) selected_book_ = 0;
    setup_ = storage_.loadSetup();
    i18n::setLanguage(i18n::resolve(setup_.language));

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
    layouts_    = layout::named(storage_.loadLayouts());
    if (ImGui::GetCurrentContext()) registerPanelSettings();
}

App::~App() {
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
    fingerprints_cancel_ = true;
    if (fingerprints_.valid()) fingerprints_.wait();
    cancelBookScan();
    for (auto& f : abandoned_scans_) f.wait();
    storage_.saveSetup(setup_);
    storage_.saveEngines(engines_);
    saveLichessToken();  // a token still being typed when the program quits
}

bool App::initGraphics() { return atlas_.create(160); }

void App::requestQuit() {
    if (run_.running()) {
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

void App::forceQuit() {
    if (run_.running()) {
        run_.stopNow();
        quit_pending_ = true;
    } else {
        quit_ = true;
    }
}

// ---------------------------------------------------------------------------
// Frame

void App::frame() {
    // Before the engines are read: nothing is read yet, so a resume at launch starts at once, and the queue reads only
    // the engines it plays first.
    if (!launch_checked_) {
        launch_checked_ = true;
        resumeAtLaunch();
    }
    pollRun();
    pollProbe();
    pollCheck();
    pollStart();
    pollOwnBooks();
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
        else if (w == "ratings" || w == "ratings-engine" || w == "ratings-h2h" || w == "ratings-progress") {
            page_                = Page::Ratings;
            ratings_tab_request_ = w == "ratings-engine" ? 1 : w == "ratings-h2h" ? 2 : w == "ratings-progress" ? 3 : 0;
        } else if (w == "books")
            page_ = Page::Books;
        else if (w == "home")
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
        else if (w.rfind("palette", 0) == 0) {  // palette=TEXT: the command palette, TEXT typed in it
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
        } else if (w == "submission-export") {  // the Export for a rating list dialog of the Games tab
            page_        = Page::Tournament;
            tab_request_ = Tab::Games;
            openSubmissionExport();
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
            page_        = Page::Tournament;
            tab_request_ = w == "live"        ? Tab::Live
                           : w == "games"     ? Tab::Games
                           : w == "openings"  ? Tab::Openings
                           : w == "standings" ? Tab::Standings
                           : w == "schedule"  ? Tab::Schedule
                           : w == "bracket"   ? Tab::Bracket
                           : w == "broadcast" ? Tab::Broadcast
                                              : Tab::Overview;
        }
        focus_done_ = true;
    }

    drawShell();
    drawPanels();
    drawSprtCalculator();
    drawQuitModal();
    drawImportPreview();
    drawRatingImport();
    drawPalette();

    // The window title tells how the run is going, also when the window is in the background.
    {
        std::string title = "fastchess-gui";
        if (run_.running()) {
            const auto& info = run_.info();
            title = trf("{} – {} · {} games", run_.paused() ? TR("Paused") : TR("Running"), info.name, games_.size());
            if (info.sprt) title += fmt::format(" · LLR {:.2f}", current_.llr);
        } else if (run_.state() == RunController::State::Finished) {
            title = trf("{} – fastchess-gui", TR("Finished"));
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
    if (run_.state() == RunController::State::Idle) {
        return trf("Ready · {} cores, {} threads", topology_.physical, topology_.logical);
    }

    if (run_.state() == RunController::State::Finished) return finish_message_;

    const auto& info = run_.info();
    std::string state;
    if (run_.stopping()) {
        state = TR("Stopping…");
    } else if (run_.paused()) {
        const int n = run_.runningGames();
        state =
            n > 0 ? trn("Paused ({} game finishing)", "Paused ({} games finishing)", n, n) : std::string(TR("Paused"));
        if (run_.stoppingWhenIdle()) state = trf("Stopping after the running games ({})", n);
    } else {
        state = TR("Running");
    }

    const auto played  = games_.size();
    const auto elapsed = std::chrono::duration<double>(Clock::now() - info.started).count();
    const auto rate    = games_per_hour_;  // refreshed once per second in pollRun()

    std::string text = trf("{} · {} / {} games · {} playing · {:.0f} games/h · {}", state, played,
                           info.total_games ? fmt::format("{}", info.total_games) : std::string("∞"),
                           run_.runningGames(), rate, ui::formatDuration(elapsed));

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
        ImGui::TextUnformatted(TR("A match is running. The games in progress will be lost."));
        ImGui::Spacing();
        if (ImGui::Button(TR("Stop and quit"))) {
            run_.stopNow();
            quit_pending_ = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TR("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (quit_pending_ && run_.running()) {
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

    // Hash tables larger than the memory installed cannot work; larger than the free memory, they swap.
    if (const auto memory = setupMemory(setup_, engines_, topology_); memory.aboveInstalled()) {
        start_error_ = memory.text();
        Console::instance().add(trf("Cannot start the match: {}", start_error_), true);
        return;
    } else if (memory.aboveAvailable()) {
        Console::instance().add(memory.text(), true);
    }

    start_error_ = run_.start(setup_, engines_, topology_, storage_.runsDir());
    if (!start_error_.empty()) {
        Console::instance().add(trf("Cannot start the match: {}", start_error_), true);
        return;
    }

    resetRunView();
    user_stop_       = false;
    resume_guarded_  = false;
    const auto& info = run_.info();
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
    start_error_ = run_.resume(unfinished_->dir);
    if (!start_error_.empty()) {
        // It did not start, which ended nothing: it is tried again at the next launch.
        if (at_launch) storage_.setRunningMarker(RunningMarker{dir, false, false});
        Console::instance().add(start_error_, true);
        return;
    }

    resetRunView();
    user_stop_      = false;
    resume_guarded_ = at_launch;
    // The games that count, as games_ holds: voided games are not shown.
    if (const auto* t = run_.tournament()) {
        const auto results = t->results();
        games_at_start_    = static_cast<std::size_t>(
            std::count_if(results.begin(), results.end(), [](const auto& r) { return !r.voided; }));
    }
    const auto& info = run_.info();
    storage_.setRunningMarker(RunningMarker{runMarkerDir(), false, at_launch});
    if (at_launch) Console::instance().add(trf("{} was running when the program ended: it resumes.", info.name));
    Console::instance().add(
        trf("Resumed: {}, {} games already played. Folder: {}", info.name, games_at_start_, info.dir.string()));
}

void App::resumeAtLaunch() {
    const auto marker = storage_.runningMarker();
    if (!marker) return;
    const auto summary    = tour::summarize(marker->dir);
    const bool unfinished = summary && !summary->finished && !(summary->total && summary->played >= *summary->total);
    // Another window of the profile plays it, and the marker is that window's: its folder is locked.
    bool busy = false;
    std::error_code ec;
    if (fs::is_directory(marker->dir, ec)) {
        try {
            const tour::FolderLock probe(marker->dir);
        } catch (const tour::FolderBusy&) {
            busy = true;
        } catch (const std::exception&) {
        }
    }
    const auto name = summary ? summary->spec.name : marker->dir;
    switch (launchResume(setup_.resume_at_launch, options_.autostart, marker, unfinished, queue_.empty(), busy)) {
        case LaunchResume::Resume:
            // The marker stays until the tournament ends: a resume that cannot start is tried again at the next
            // launch. resumeRun() says that it resumes once it does.
            unfinished_ = summary;
            resumeRun(true);
            break;
        case LaunchResume::Busy:
            Console::instance().add(trf("{} is playing in another window: it is not resumed here.", name));
            break;
        case LaunchResume::Crashed:
            // Resuming it again could end the program again, at every launch: the user decides.
            storage_.setRunningMarker(std::nullopt);
            unfinished_     = summary;
            resume_crashed_ = fs::absolute(summary->dir).lexically_normal();
            Console::instance().add(trf("The program ended while it resumed {} at the last launch: it is not resumed "
                                        "automatically again. Resume it from the Tournaments page.",
                                        name),
                                    true);
            break;
        case LaunchResume::Queue:
            storage_.setRunningMarker(std::nullopt);
            Console::instance().add(TR("The tournament running when the program ended is over: the queue goes on."));
            startFromQueue(true);
            break;
        case LaunchResume::Nothing:
            // Paused, or not to be resumed at launch: it waits for the user, as any interrupted tournament.
            storage_.setRunningMarker(std::nullopt);
            break;
    }
}

void App::setRunPaused(bool paused) {
    run_.setPaused(paused);
    if (quick_test_engine_.empty() && !user_stop_)
        storage_.setRunningMarker(RunningMarker{runMarkerDir(), paused, resume_guarded_});
}

void App::stopRun(bool now) {
    if (!run_.running()) return;
    user_stop_ = true;
    if (now) {
        run_.stopNow();
    } else {
        run_.stopWhenIdle();
    }
    // Stopped, not interrupted: not resumed at the next launch, even if the program ends before the engines stop.
    storage_.clearRunningMarker(runMarkerDir());
}

std::string App::runMarkerDir() const { return fs::absolute(run_.info().dir).lexically_normal().string(); }

void App::resetRunView() {
    page_ = Page::Tournament;
    select_game_id_.reset();
    // On its overview, when the panels are tabs of one node; side by side, they stay as they are.
    if (const auto* root = ImGui::DockBuilderGetNode(workspaceDock());
        panelOpen(layout::panel::OVERVIEW) && (!root || !root->IsSplitNode()))
        tab_request_ = Tab::Overview;
    tournaments_dirty_ = true;
    unfinished_.reset();
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
    finish_message_.clear();

    const auto& info = run_.info();
    sprt_.reset();
    if (info.sprt) sprt_.emplace(info.alpha, info.beta, info.elo0, info.elo1, info.model, true);

    live_slot_        = std::clamp(live_slot_, 0, info.concurrency - 1);
    games_per_hour_   = 0;
    games_at_start_   = 0;
    last_rate_update_ = {};
}

void App::pollRun() {
    run_.update();

    if (auto* obs = run_.observer()) {
        std::vector<fastchess::Stats> fresh;
        const auto before = games_.size();
        obs->drain(games_, fresh);

        for (const auto& s : fresh) {
            stats_ = s;
            history_.push_back(computeStats(s, run_.info().penta, sprt_ ? &*sprt_ : nullptr));
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
    if (run_.running()) {
        const auto now = Clock::now();
        if (now - last_rate_update_ >= std::chrono::seconds(1)) {
            const auto elapsed = std::chrono::duration<double>(now - run_.info().started).count();
            // games_ can get below games_at_start_ when games are voided meanwhile.
            const auto played = games_.size() > games_at_start_ ? games_.size() - games_at_start_ : 0;
            games_per_hour_   = elapsed > 5 ? static_cast<double>(played) / (elapsed / 3600.0) : 0.0;
            last_rate_update_ = now;
        }
    }

    // A resume at launch that has played a while did not end the program: the next launch may resume it again.
    if (resume_guarded_ && run_.running() && Clock::now() - run_.info().started >= std::chrono::seconds(30)) {
        resume_guarded_ = false;
        if (auto marker = storage_.runningMarker(); markerNames(marker, runMarkerDir())) {
            marker->resuming = false;
            storage_.setRunningMarker(marker);
        }
    }

    if (run_.takeFinishedEvent()) onRunFinished();

    if (quit_pending_ && !run_.running()) quit_ = true;
}

void App::onRunFinished() {
    if (!quick_test_engine_.empty()) {
        finishQuickTest();
        return;
    }
    const auto& info  = run_.info();
    const auto error  = run_.error();
    const auto played = stats_.sum();

    // Quitting while it runs leaves it to be resumed at the next launch; otherwise it ended, or the user stopped it.
    // Only this window's marker: another window of the profile may have written its own since.
    const auto marker_dir = runMarkerDir();
    if (!keepMarkerAtEnd(quit_pending_, user_stop_)) {
        storage_.clearRunningMarker(marker_dir);
    } else if (auto marker = storage_.runningMarker(); markerNames(marker, marker_dir) && marker->resuming) {
        marker->resuming = false;  // quit by the user: the resume did not end the program
        storage_.setRunningMarker(marker);
    }
    resume_guarded_ = false;

    std::string verdict;
    if (!error.empty()) {
        verdict = trf("Error: {}", error);
    } else if (fastchess::atomic::abnormal_termination && !run_.stoppedByUser()) {
        verdict = TR("Interrupted: an engine crashed or stopped responding (see Console)");
    } else if (info.sprt && sprt_ && current_.llr >= sprt_->getUpperBound()) {
        verdict = trf("SPRT finished: H1 accepted, {} is stronger", info.engine_a);
    } else if (info.sprt && sprt_ && current_.llr <= sprt_->getLowerBound()) {
        verdict = trf("SPRT finished: H0 accepted, {} is not stronger", info.engine_a);
    } else if (run_.stoppedByUser()) {
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
        if (const auto* t = run_.tournament(); t && t->spec().format == tour::Format::Knockout && t->finished()) {
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

    Console::instance().add(finish_message_, !error.empty());

    // A stopped or interrupted match can be continued later.
    unfinished_        = unfinishedMatch(storage_.runsDir());
    tournaments_dirty_ = true;

    if (setup_.notify) {
        notifyRunFinished(window_, info.name, finish_message_);
    }

    // The queue goes on after a tournament that ended by itself, not after a stop or an error.
    const bool completed = error.empty() && !run_.stoppedByUser() && !fastchess::atomic::abnormal_termination;
    if (completed && !queue_.empty() && !quit_pending_) startFromQueue(true);
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
    if (run_.running()) return;
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
        setup_ = setup;
        startRun();
        // Taken off the queue once started: one that cannot start (an engine removed since...) is not lost.
        if (run_.running()) {
            queue_.erase(queue_.begin() + static_cast<std::ptrdiff_t>(i));
            storage_.saveQueue(queue_);
            return;
        }
        if (!go_on) return;
    }
}

std::string App::summaryText() const {
    const auto& info = run_.info();
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
                        out << *text;
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
                out << tour::openingsEpd(shown);
                Console::instance().add(
                    out ? trn("{} opening written to {}", "{} openings written to {}", shown.size(), shown.size(), path)
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
                if (const auto* t = run_.tournament()) {
                    std::ofstream out(path, std::ios::binary | std::ios::trunc);
                    out << tour::tableHtml(shown_, t->spec().name,
                                           tour::footerLine(t->spec(), shown_, t->totalGames()));
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
            case Dialogs::Purpose::ExportGames:
                exportGames(path, false);
                break;
            case Dialogs::Purpose::ExportAll:
                exportGames(path, true);
                break;
            case Dialogs::Purpose::ExportSubmission:
                writeSubmissionExport(path);
                break;
        }
    }
}

}  // namespace fcg
