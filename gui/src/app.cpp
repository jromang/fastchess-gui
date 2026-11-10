#include "app.hpp"

#include <fstream>

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <fmt/include/fmt/format.h>
#include <core/globals/globals.hpp>

#include "i18n.hpp"
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
    engines_ = storage_.loadEngines();
    setup_   = storage_.loadSetup();
    i18n::setLanguage(i18n::resolve(setup_.language));

    if (!setup_.concurrency_auto) setup_.concurrency = std::clamp(setup_.concurrency, 1, topology_.logical);

    if (!engines_.empty()) selected_engine_ = 0;

    refreshPrograms();

    // Engines added by hand to engines.json, or never probed, get their options read in the background.
    for (const auto& e : engines_) {
        if (e.uci_options.empty() && !e.cmd.empty()) probe_queue_.push_back(e.name);
    }

    Console::instance().add(
        trf("fastchess-gui {} — {} physical cores, {} threads", FCG_VERSION, topology_.physical, topology_.logical));

    unfinished_ = unfinishedMatch(storage_.runsDir());
    queue_      = storage_.loadQueue();
}

App::~App() {
    storage_.saveSetup(setup_);
    storage_.saveEngines(engines_);
}

bool App::initGraphics() { return atlas_.create(160); }

void App::requestQuit() {
    if (run_.running()) {
        open_quit_modal_ = true;
    } else {
        quit_ = true;
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
    pollRun();
    pollProbe();
    pollCheck();
    handleDialogs();

    if (options_.autostart && !autostart_done_ && !probe_.valid() && probe_queue_.empty()) {
        autostart_done_ = true;
        startRun();
    }

    applyThemeSetting();
    updateBroadcast();

    // --shot and --focus: show the requested page or tab once the first frames are drawn.
    if (!options_.focus_window.empty() && !focus_done_) {
        const auto& w = options_.focus_window;
        if (w == "setup")
            page_ = Page::NewMatch;
        else if (w == "engines")
            page_ = Page::Engines;
        else if (w == "console")
            page_ = Page::Log;
        else if (w == "settings")
            page_ = Page::Settings;
        else if (w == "ratings")
            page_ = Page::Ratings;
        else if (w == "home")
            page_ = Page::Home;
        else {
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
    drawDetached();
    drawQuitModal();

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
        state = n > 1 ? trf("Paused ({} games finishing)", n) : n == 1 ? TR("Paused (1 game finishing)") : TR("Paused");
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

    start_error_ = run_.start(setup_, engines_, topology_, storage_.runsDir());
    if (!start_error_.empty()) {
        Console::instance().add(trf("Cannot start the match: {}", start_error_), true);
        return;
    }

    resetRunView();
    const auto& info = run_.info();
    Console::instance().add(trf("Started: {}, {}, {} concurrent games{}. Folder: {}", info.name, info.limit_text,
                                info.concurrency, info.affinity ? TR(", CPU affinity") : "", info.dir.string()));
}

void App::resumeRun() {
    if (!unfinished_) return;

    start_error_ = run_.resume(unfinished_->dir);
    if (!start_error_.empty()) {
        Console::instance().add(start_error_, true);
        return;
    }

    resetRunView();
    games_at_start_  = run_.tournament() ? run_.tournament()->results().size() : 0;
    const auto& info = run_.info();
    Console::instance().add(
        trf("Resumed: {}, {} games already played. Folder: {}", info.name, games_at_start_, info.dir.string()));
}

void App::resetRunView() {
    page_              = Page::Tournament;
    tab_request_       = Tab::Overview;
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
    selected_opening_ = -1;
    totals_.clear();
    table_     = {};
    table_for_ = ~std::size_t(0);
    anchor_.reset();
    crosstable_cell_.reset();
    games_subject_ = -1;
    filtered_.clear();
    filtered_for_  = ~std::size_t(0);
    selected_game_ = -1;
    replay_.reset();
    slot_views_.clear();
    detached_.clear();
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
            totals_      = obs->totals();
            last_totals_ = now;
        }
    }

    // Games per hour, recomputed at most once per second so the number does not flicker.
    if (run_.running()) {
        const auto now = Clock::now();
        if (now - last_rate_update_ >= std::chrono::seconds(1)) {
            const auto elapsed = std::chrono::duration<double>(now - run_.info().started).count();
            games_per_hour_    = elapsed > 5 ? (games_.size() - games_at_start_) / (elapsed / 3600.0) : 0.0;
            last_rate_update_  = now;
        }
    }

    if (run_.takeFinishedEvent()) onRunFinished();

    if (quit_pending_ && !run_.running()) quit_ = true;
}

void App::onRunFinished() {
    const auto& info  = run_.info();
    const auto error  = run_.error();
    const auto played = stats_.sum();

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
    if (completed && !queue_.empty() && !quit_pending_) startFromQueue();
}

void App::enqueue() {
    QueuedTournament item;
    item.setup = setup_;
    item.label = setup_.name.empty() ? defaultTournamentName(setup_) : setup_.name;
    queue_.push_back(item);
    storage_.saveQueue(queue_);
    Console::instance().add(trf("Queued: {}", item.label));
}

void App::startFromQueue() {
    if (queue_.empty() || run_.running()) return;
    auto next = queue_.front().setup;
    // The look of the program follows the settings, not the queued setup.
    next.language = setup_.language;
    next.theme    = setup_.theme;
    next.notify   = setup_.notify;
    setup_        = next;
    queue_.erase(queue_.begin());
    storage_.saveQueue(queue_);
    startRun();
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
                ratings_dirty_     = true;
                unfinished_        = unfinishedMatch(storage_.runsDir());
                break;
            case Dialogs::Purpose::ExportRatings: {
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out << tour::ratingListCsv(rating_list_);
                Console::instance().add(out ? trf("Rating list written to {}", path) : trf("Cannot write {}", path),
                                        !out);
                break;
            }
            case Dialogs::Purpose::ExportOpenings: {
                const auto shown = shownOpenings();
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out << tour::openingsEpd(shown);
                Console::instance().add(
                    out ? trf("{} openings written to {}", shown.size(), path) : trf("Cannot write {}", path), !out);
                break;
            }
            case Dialogs::Purpose::ImportEngineList:
                importEngines(importEngineList(path));
                break;
            case Dialogs::Purpose::ImportEngineFolder:
                importEngines(findEngines(path));
                break;
            case Dialogs::Purpose::OpenTournament:
                openTournament(path);
                break;
            case Dialogs::Purpose::ExportStandings:
                if (const auto* t = run_.tournament()) {
                    std::ofstream out(path, std::ios::binary | std::ios::trunc);
                    out << tour::tableHtml(table_, t->spec().name);
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
            case Dialogs::Purpose::Book:
                setup_.book = path;
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
        }
    }
}

}  // namespace fcg
