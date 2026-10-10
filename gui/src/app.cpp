#include "app.hpp"

#include <SDL3/SDL.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <fmt/include/fmt/core.h>
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

    // Engines added by hand to engines.json, or never probed, get their options read in the background.
    for (const auto& e : engines_) {
        if (e.uci_options.empty() && !e.cmd.empty()) probe_queue_.push_back(e.name);
    }

    Console::instance().add(
        trf("fastchess-gui {} — {} physical cores, {} threads", FCG_VERSION, topology_.physical, topology_.logical));
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
    handleDialogs();

    if (options_.autostart && !autostart_done_ && !probe_.valid() && probe_queue_.empty()) {
        autostart_done_ = true;
        startRun();
    }

    drawToolbar();

    const ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_None);
    if (!layout_checked_) {
        layout_checked_ = true;
        auto* node      = ImGui::DockBuilderGetNode(dockspace);
        if (!node || (!node->IsSplitNode() && node->Windows.Size == 0)) buildDefaultLayout(dockspace);
    }

    drawSetup();
    drawLive();
    drawGames();
    drawResults();
    drawCharts();
    drawEngines();
    drawConsole();
    drawQuitModal();

    if (!options_.focus_window.empty() && !focus_done_ && ImGui::GetFrameCount() > 2) {
        const auto name = "###" + options_.focus_window;  // windows are known by their stable id
        if (auto* w = ImGui::FindWindowByName(name.c_str())) {
            if (w->DockNode && w->DockNode->TabBar) w->DockNode->TabBar->NextSelectedTabId = w->TabId;
        }
        ImGui::SetWindowFocus(name.c_str());
        focus_done_ = true;
    }

    // The window title tells how the run is going, also when the window is in the background.
    {
        std::string title = "fastchess-gui";
        if (run_.running()) {
            const auto& info = run_.info();
            title = trf("{} – {} vs {} · {} games", run_.paused() ? TR("Paused") : TR("Running"), info.engine_a,
                        info.engine_b, games_.size());
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

void App::buildDefaultLayout(unsigned int dockspace) {
    const auto* viewport = ImGui::GetMainViewport();

    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);

    ImGuiID left = 0, rest = 0, bottom = 0, center = 0;
    ImGui::DockBuilderSplitNode(dockspace, ImGuiDir_Left, 0.27f, &left, &rest);
    ImGui::DockBuilderSplitNode(rest, ImGuiDir_Down, 0.27f, &bottom, &center);

    // Windows are known by their "###id", which does not change with the language.
    ImGui::DockBuilderDockWindow("###setup", left);
    ImGui::DockBuilderDockWindow("###live", center);
    ImGui::DockBuilderDockWindow("###games", center);
    ImGui::DockBuilderDockWindow("###charts", center);
    ImGui::DockBuilderDockWindow("###engines", center);
    ImGui::DockBuilderDockWindow("###console", center);
    ImGui::DockBuilderDockWindow("###results", bottom);
    ImGui::DockBuilderFinish(dockspace);
}

// ---------------------------------------------------------------------------
// Toolbar

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

void App::drawToolbar() {
    if (!ImGui::BeginMainMenuBar()) return;

    const bool running = run_.running();

    if (!running) {
        const bool engines_set = !setup_.engine_a.empty() && !setup_.engine_b.empty();
        const bool probing     = probe_.valid() || !probe_queue_.empty();
        if (ui::iconButton("start", ui::Icon::Play, TR("Start"), engines_set && !probing)) startRun();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (!engines_set)
                ImGui::SetTooltip("%s", TR("Choose engines A and B in Setup."));
            else if (probing)
                ImGui::SetTooltip("%s", TR("Reading the options of an engine…"));
        }
    } else {
        const bool paused = run_.paused();
        if (paused) {
            if (ui::iconButton("resume", ui::Icon::Play, TR("Resume"), !run_.stopping() && !run_.stoppingWhenIdle())) {
                run_.setPaused(false);
            }
        } else {
            if (ui::iconButton("pause", ui::Icon::Pause, TR("Pause"), !run_.stopping())) run_.setPaused(true);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("Pause: no new pair starts. Running games, and the second game of their "
                                       "pair,\nplay to the end (pair statistics stay complete)."));
        }

        if (ui::iconButton("stop", ui::Icon::Stop, TR("Stop"), !run_.stopping())) ImGui::OpenPopup("stop_menu");
        if (ImGui::BeginPopup("stop_menu")) {
            if (ImGui::MenuItem(TR("Finish the running games, then stop"))) run_.stopWhenIdle();
            if (ImGui::MenuItem(TR("Stop now (running games are lost)"))) run_.stopNow();
            ImGui::EndPopup();
        }
    }

    ImGui::Separator();

    const auto status = runStatusText();
    if (run_.state() == RunController::State::Finished && !run_.error().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextUnformatted(status.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextUnformatted(status.c_str());
    }

    if (!start_error_.empty()) {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextUnformatted(start_error_.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) start_error_.clear();
    }

    ImGui::EndMainMenuBar();
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

    games_.clear();
    history_.clear();
    stats_   = {};
    current_ = {};
    reasons_.clear();
    totals_ = {};
    filtered_.clear();
    filtered_for_  = ~std::size_t(0);
    selected_game_ = -1;
    replay_.reset();
    live_               = {};
    live_board_version_ = ~0ULL;
    finish_message_.clear();

    const auto& info = run_.info();
    sprt_.reset();
    if (info.sprt) sprt_.emplace(info.alpha, info.beta, info.elo0, info.elo1, info.model, true);

    live_slot_        = std::clamp(live_slot_, 0, info.concurrency - 1);
    games_per_hour_   = 0;
    last_rate_update_ = {};

    Console::instance().add(trf("Match started: {} vs {}, {}, {} concurrent games{}. Folder: {}", info.engine_a,
                                info.engine_b, info.limit_text, info.concurrency,
                                info.affinity ? TR(", CPU affinity") : "", info.dir.string()));
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
            if (games_.size() != before) reasons_ = obs->reasons();
            totals_      = obs->totals();
            last_totals_ = now;
        }
    }

    // Games per hour, recomputed at most once per second so the number does not flicker.
    if (run_.running()) {
        const auto now = Clock::now();
        if (now - last_rate_update_ >= std::chrono::seconds(1)) {
            const auto elapsed = std::chrono::duration<double>(now - run_.info().started).count();
            games_per_hour_    = elapsed > 5 ? games_.size() / (elapsed / 3600.0) : 0.0;
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

    finish_message_ = trf("{} · {} games · Elo {} · {}", verdict, played,
                          current_.valid ? formatElo(current_.elo, current_.elo_err) : std::string("—"),
                          ui::formatDuration(std::chrono::duration<double>(info.finished - info.started).count()));
    if (info.sprt) finish_message_ += fmt::format(" · LLR {:.2f}", current_.llr);

    Console::instance().add(finish_message_, !error.empty());

    if (setup_.notify) {
        notifyRunFinished(window_, trf("{} vs {}", info.engine_a, info.engine_b), finish_message_);
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
            case Dialogs::Purpose::EngineCommand:
                if (selected_engine_ >= 0 && selected_engine_ < static_cast<int>(engines_.size())) {
                    engines_[selected_engine_].cmd = path;
                    storage_.saveEngines(engines_);
                    startProbe(selected_engine_);
                }
                break;
            case Dialogs::Purpose::EngineDir:
                if (selected_engine_ >= 0 && selected_engine_ < static_cast<int>(engines_.size())) {
                    engines_[selected_engine_].dir = path;
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
