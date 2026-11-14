// The application shell: navigation rail and pages (home, new match, tournament workspace, engines, log,
// settings). The tournament views themselves live in view_*.cpp and draw inside these pages.

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <tour/legacy.hpp>

#include "app.hpp"
#include "console.hpp"
#include "gl.hpp"
#include "home.hpp"
#include "i18n.hpp"
#include "logo.hpp"
#include "memory.hpp"
#include "report.hpp"
#include "setup_checks.hpp"
#include "renderer.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

std::string folderDate(const fs::path& dir) {
    const auto name = dir.filename().string();
    if (name.size() < 15 || name[8] != '-') return "";
    return name.substr(0, 4) + "-" + name.substr(4, 2) + "-" + name.substr(6, 2) + " " + name.substr(9, 2) + ":" +
           name.substr(11, 2);
}

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

using home::formatLabel;
using home::statusLabel;

void openFolder(const fs::path& dir) { ui::openPath(dir); }

// The interface sizes offered, in percent.
constexpr std::array<int, 9> ZOOMS = {80, 90, 100, 110, 125, 150, 175, 200, 250};

using home::endTime;
using home::sectionTitle;

}  // namespace

void App::applyThemeSetting() {
    ui::Theme wanted = ui::Theme::Light;
    if (setup_.theme == "dark") {
        wanted = ui::Theme::Dark;
    } else if (setup_.theme == "system") {
        wanted = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_LIGHT ? ui::Theme::Light : ui::Theme::Dark;
    }
    const int zoom = std::clamp(setup_.ui_zoom, ZOOMS.front(), ZOOMS.back());
    if (theme_applied_ == wanted && zoom_applied_ == zoom) return;

    // The fonts follow FontScaleMain; paddings, spacings and roundings are rebuilt at the new scale.
    ui::applyTheme(wanted, options_.ui_scale * zoom / 100.0f);
    ImGui::GetStyle().FontScaleMain = zoom / 100.0f;
    theme_applied_                  = wanted;
    zoom_applied_                   = zoom;
}

void App::onDisplayScale(float scale) {
    if (scale == options_.ui_scale) return;
    options_.ui_scale              = scale;
    ImGui::GetStyle().FontScaleDpi = scale;
    theme_applied_.reset();  // the style is rebuilt at the new scale on the next frame
}

// Ctrl + and Ctrl -, or Ctrl and the mouse wheel, step through the sizes; Ctrl 0 goes back to 100 %.
void App::handleZoomKeys() {
    // The wheel is taken even under a popup, where it does not zoom: with Ctrl held it never zooms a chart.
    const int wheel = ui::takeCtrlWheel();
    if (!ImGui::GetIO().KeyCtrl) return;
    int step = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) step = 1;
    if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) step = -1;
    if (wheel != 0 && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) step = wheel;

    int zoom = setup_.ui_zoom;
    if (ImGui::IsKeyPressed(ImGuiKey_0) || ImGui::IsKeyPressed(ImGuiKey_Keypad0)) {
        zoom = 100;
    } else if (step > 0) {
        const auto* next = std::upper_bound(ZOOMS.begin(), ZOOMS.end(), zoom);
        if (next != ZOOMS.end()) zoom = *next;
    } else if (step < 0) {
        const auto* next = std::lower_bound(ZOOMS.begin(), ZOOMS.end(), zoom);
        if (next != ZOOMS.begin()) zoom = *(next - 1);
    }
    if (zoom == setup_.ui_zoom) return;
    setup_.ui_zoom = zoom;
    saveSetupSoon();
}

void App::refreshTournaments() {
    if (tournaments_job_.valid()) {
        if (tournaments_job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        auto list         = tournaments_job_.get();
        tournaments_      = std::move(list.summaries);
        legacy_runs_      = list.legacy_runs;
        tournaments_read_ = true;
        played_programs_  = lastPlayedPrograms(tournaments_);
    }
    if (!tournaments_dirty_) return;

    // On a worker thread: hundreds of journals take a moment, and a folder on a network drive longer.
    tournaments_job_   = std::async(std::launch::async, [cache = summary_cache_, root = storage_.runsDir()] {
        return TournamentList{cache->list(root), tour::legacyRuns(root).size()};
    });
    tournaments_dirty_ = false;
}

void App::importLegacyRuns() {
    int imported = 0;
    for (const auto& dir : tour::legacyRuns(storage_.runsDir())) {
        try {
            tour::importLegacyRun(dir);
            ++imported;
        } catch (const std::exception& e) {
            Console::instance().add(trf("Cannot import {}: {}", dir.string(), e.what()), true);
        }
    }
    Console::instance().add(trn("{} earlier match imported.", "{} earlier matches imported.", imported, imported));
    tournaments_dirty_ = true;
}

void App::openTournament(const fs::path& dir) {
    if (run_.running()) {
        if (run_.tournament() && run_.tournament()->dir() == dir) {
            page_ = Page::Tournament;
        } else {
            start_error_ = TR("A match is running: wait for its end to open another one.");
        }
        return;
    }

    start_error_ = run_.view(dir);
    if (!start_error_.empty()) return;

    const auto summary = tour::summarize(dir);
    resetRunView();
    if (summary) {
        finish_message_ = trf("{} · {} games", statusLabel(*summary, false), summary->played);
        if (!summary->finished) unfinished_ = summary;
    }
}

// ---------------------------------------------------------------------------

void App::drawShell() {
    if (i18n::language() == i18n::Language::Chinese) ui::requestCjkFont();
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking;
    ImGui::Begin("##shell", nullptr, flags);
    ImGui::PopStyleVar(2);

    const float rail = px(76);
    drawRail(rail);
    ImGui::SameLine(0, 0);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(24), px(18)));
    ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    switch (page_) {
        case Page::Home:
            drawHome();
            break;
        case Page::NewMatch:
            drawNewMatch();
            break;
        case Page::Tournament:
            drawWorkspace();
            break;
        case Page::Engines:
            ui::pageTitle(TR("Engines"), guideSection());
            drawEngines();
            break;
        case Page::Books:
            ui::pageTitle(TR("Books"), guideSection());
            drawBooks();
            break;
        case Page::Board:
            drawBoardPage();
            break;
        case Page::Suites:
            ui::pageTitle(TR("Test suites"), guideSection());
            drawSuites();
            break;
        case Page::Ratings:
            drawRatings();
            break;
        case Page::Log:
            ui::pageTitle(TR("Log"), guideSection());
            drawConsole();
            break;
        case Page::Settings:
            drawSettings();
            break;
    }
    // The workspace's dock space stays alive on the other pages: the panels docked in it stay there. Also when the
    // page changed to the workspace's during this frame (Open on the list of tournaments), after the switch above.
    layout::keepAlive(workspaceDock());
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ui::drawOpenPathError();

    ImGui::End();
}

int App::gamesPlaying() const {
    const auto* obs = run_.observer();
    if (!obs || !run_.running()) return 0;
    int n = 0;
    for (int i = 0; i < obs->slotCount(); ++i) n += obs->slotActive(i) ? 1 : 0;
    return n;
}

void App::drawRail(float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::RAIL_BG));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, px(14)));
    ImGui::BeginChild("##rail", ImVec2(width, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    // Nine buttons under the logo: lower ones in a low window, rather than a scroll bar.
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const float button  = std::clamp((ImGui::GetContentRegionAvail().y - px(48) - 9 * spacing) / 9,
                                     ImGui::GetFontSize() * 2.3f, ui::railButtonHeight());

    // The logo, on the accent colour.
    {
        const auto pos   = ImGui::GetCursorScreenPos();
        const float size = px(36);
        logo::draw(ImGui::GetWindowDrawList(), ImVec2(pos.x + (width - size) * 0.5f, pos.y), size, ui::ACCENT,
                   ui::currentTheme() == ui::Theme::Dark ? IM_COL32(11, 18, 32, 255) : IM_COL32_WHITE);
        ImGui::Dummy(ImVec2(width, size + px(12)));
    }

    const bool in_tournaments = page_ == Page::Home || page_ == Page::NewMatch || page_ == Page::Tournament;
    const int playing         = gamesPlaying();
    if (ui::railButton("rail_home", ui::Icon::Trophy, TR("Tournaments"), in_tournaments, width, button)) {
        page_              = Page::Home;
        tournaments_dirty_ = true;
    }
    if (playing > 0) {
        // The games being played, at the icon's top right.
        const auto min   = ImGui::GetItemRectMin();
        const float font = ImGui::GetFontSize();
        const auto count = std::to_string(playing);
        ui::drawBadge(ImGui::GetWindowDrawList(),
                      ImVec2(min.x + width * 0.5f + font * 0.75f, min.y + button * 0.36f - font * 0.5f), count.c_str());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", trn("{} game being played", "{} games being played", playing, playing).c_str());
        }
    }
    if (ui::railButton("rail_engines", ui::Icon::Engine, TR("Engines"), page_ == Page::Engines, width, button)) {
        page_ = Page::Engines;
    }
    if (ui::railButton("rail_books", ui::Icon::Book, TR("Books"), page_ == Page::Books, width, button))
        page_ = Page::Books;
    if (ui::railButton("rail_board", ui::Icon::Board, TR("Board"), page_ == Page::Board, width, button))
        page_ = Page::Board;
    if (board_.analysing || board_.analyser.active()) {
        // The analysis runs: a dot at the icon's top right.
        const auto min = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(min.x + width * 0.5f + ImGui::GetFontSize() * 0.75f, min.y + button * 0.3f),
            ImGui::GetFontSize() * 0.22f, board_.analysing ? ui::GOOD : ui::MUTED);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", board_.analysing ? TR("The analysis is running.") : TR("The analysis engine waits."));
        }
    }
    if (ui::railButton("rail_suites", ui::Icon::Suite, TR("Suites"), page_ == Page::Suites, width, button)) {
        page_ = Page::Suites;
    }
    if (ui::railButton("rail_ratings", ui::Icon::Chart, TR("Ratings"), page_ == Page::Ratings, width, button)) {
        page_          = Page::Ratings;
        game_db_dirty_ = true;
    }
    if (ui::railButton("rail_log", ui::Icon::Log, TR("Log"), page_ == Page::Log, width, button)) page_ = Page::Log;
    if (ui::railButton("rail_settings", ui::Icon::Settings, TR("Settings"), page_ == Page::Settings, width, button)) {
        page_ = Page::Settings;
    }

    // Quit, at the bottom: the window's own close button is not always in reach.
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - button - px(14)));
    if (ui::railButton("rail_quit", ui::Icon::Quit, TR("Quit"), false, width, button)) requestQuit();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// New match

bool App::setupChosen() const {
    return setup_.format == TournamentFormat::Match ? !setup_.engine_a.empty() && !setup_.engine_b.empty()
                                                    : setup_.participants.size() >= 2;
}

// SPRT bounds the wrong way round, said beside them, rather than fastchess' message once started.
bool App::setupBadSprt() const {
    return setup_.format == TournamentFormat::Match && setup_.mode == MatchMode::Sprt &&
           setup_.sprt_elo1 <= setup_.sprt_elo0;
}

// Only the engines of this setup matter: the others' options can be read while it plays.
bool App::setupProbing() const {
    if (setup_.format == TournamentFormat::Match) return probing(setup_.engine_a) || probing(setup_.engine_b);
    return std::any_of(setup_.participants.begin(), setup_.participants.end(),
                       [&](const ParticipantSetup& p) { return probing(p.name); });
}

void App::drawNewMatch() {
    const bool locked = run_.running();

    ui::pageTitle(TR("New tournament"), guideSection());
    ImGui::TextDisabled("%s", TR("A match between two versions, a round robin or a gauntlet."));

    const bool chosen   = setupChosen();
    const bool bad_sprt = setupBadSprt();
    const bool ready    = chosen && !bad_sprt;
    const bool probing  = setupProbing();
    // While an engine of the setup is read, Start waits for it (and says so), see startAfterReads.
    if (!drawStartWaiting("start", "start")) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
        ImGui::PushStyleColor(
            ImGuiCol_Text, ui::currentTheme() == ui::Theme::Dark ? ImVec4(0.04f, 0.07f, 0.13f, 1) : ImVec4(1, 1, 1, 1));
        if (ui::iconButton("start", ui::Icon::Play, TR("Start"), ready && !locked)) startRun();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (locked)
                ImGui::SetTooltip("%s", TR("A match is already running."));
            else if (!chosen)
                ImGui::SetTooltip("%s", setup_.format == TournamentFormat::Match ? TR("Choose engines A and B.")
                                                                                 : TR("Choose at least two engines."));
            else if (bad_sprt)
                ImGui::SetTooltip("%s", TR("elo1 must be larger than elo0."));
            else if (probing)
                ImGui::SetTooltip("%s", TR("Reading the options of an engine…"));
        }
        ImGui::PopStyleColor(2);
    }
    ImGui::SameLine();
    if (ui::iconButton("enqueue", ui::Icon::Plus, TR("Add to the queue"), ready)) enqueue();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", bad_sprt ? TR("elo1 must be larger than elo0.")
                                         : TR("Starts by itself when the running tournament ends."));
    }
    // What no profile holds, beside the buttons: the template the setup came from, the tournament's name.
    ImGui::BeginDisabled(locked);
    ui::sameLineOrWrap(px(200));
    drawTemplates();
    ui::sameLineOrWrap(px(380));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(TR("Name"));
    ImGui::SameLine();
    {
        char buffer[256];
        const auto n = std::min(setup_.name.size(), sizeof(buffer) - 1);
        std::copy_n(setup_.name.data(), n, buffer);
        buffer[n] = 0;
        ImGui::SetNextItemWidth(px(320));
        if (ImGui::InputTextWithHint("##tname", defaultTournamentName(setup_).c_str(), buffer, sizeof(buffer))) {
            setup_.name = buffer;
            saveSetupSoon();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && setup_.name.empty())
            ImGui::SetTooltip("%s", TR("Empty: a name made from the engines."));
    }
    ImGui::EndDisabled();
    if (!start_error_.empty()) {
        drawMessage(start_error_, start_fixes_, start_fixes_for_, true, "start_error");
    }
    if (const auto waiting = startWaitingText("start"); !waiting.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", waiting.c_str());
    }
    ImGui::Spacing();

    // The summary on the right, the form beside it, centred when it leaves room; the participants and the library
    // side by side when both are wide enough to read. A narrow window gets the summary under the form, in the same
    // scroll.
    const float avail   = ImGui::GetContentRegionAvail().x;
    const float gap     = px(20);
    const float summary = std::clamp(avail * 0.24f, px(300), px(400));
    const float rest    = avail - summary - gap;
    const bool side     = rest >= px(560);
    const float width   = side ? std::min(rest, px(1300)) : avail;
    const bool two      = width >= px(700);
    if (side) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::floor((rest - width) * 0.5f));

    ImGui::BeginChild("##new_match", ImVec2(width, 0));
    drawSetup(two, !side);
    ImGui::EndChild();

    if (side) {
        ImGui::SameLine(0, gap);
        ImGui::BeginChild("##new_summary", ImVec2(summary, 0));
        drawSetupSummary();
        ImGui::EndChild();
    }
}

// ---------------------------------------------------------------------------
// Tournament workspace

void App::drawWorkspaceHeader() {
    const auto& info = run_.info();
    const auto* t    = run_.tournament();

    const auto detail = info.format == tour::Format::Match
                            ? (info.sprt ? fmt::format("SPRT [{:g}, {:g}] {}", info.elo0, info.elo1, info.model)
                               : info.total_games ? trn("{} game", "{} games", info.total_games, info.total_games)
                                                  : std::string(TR("until stopped")))
                            : trf("{} engines · {} games", info.participants, info.total_games);
    ImGui::TextDisabled("%s", fmt::format("{} · {} · {}", formatLabel(info.format), info.limit_text, detail).c_str());
    ui::pageTitle(t ? t->spec().name.c_str() : TR("No match"), guideSection());

    const auto status   = runStatusText();
    const bool finished = run_.state() == RunController::State::Finished;
    // Ended by an error or an engine that failed: which engine, with Open the engine and Open the log.
    if (finished && (!run_.error().empty() || finish_fixes_for_ == status)) {
        drawMessage(status, finish_fixes_, finish_fixes_for_, true, "finish");
    } else if (finished && t && t->startFailures().stalled()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        ImGui::TextWrapped("%s", status.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextUnformatted(status.c_str());
    }

    if (info.total_games > 0 && t) {
        ImGui::ProgressBar(std::min(1.0f, static_cast<float>(games_.size()) / static_cast<float>(info.total_games)),
                           ImVec2(-FLT_MIN, px(6)), "");
    }
    if (drawStartFailures()) ImGui::Spacing();
    drawResumedPrograms();

    if (run_.running()) {
        const bool paused = run_.paused();
        if (paused) {
            if (ui::iconButton("resume", ui::Icon::Play, TR("Resume"), !run_.stopping() && !run_.stoppingWhenIdle())) {
                setRunPaused(false);
            }
        } else {
            if (ui::iconButton("pause", ui::Icon::Pause, TR("Pause"), !run_.stopping())) setRunPaused(true);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("Pause: no new pair starts. Running games, and the second game of their "
                                       "pair,\nplay to the end (pair statistics stay complete)."));
        }

        ImGui::SameLine();
        if (ui::iconButton("stop", ui::Icon::Stop, TR("Stop"), !run_.stopping())) ImGui::OpenPopup("stop_menu");
        if (ImGui::BeginPopup("stop_menu")) {
            if (ImGui::MenuItem(TR("Finish the running games, then stop"))) stopRun(false);
            if (ImGui::MenuItem(TR("Stop now (running games are lost)"))) stopRun(true);
            ImGui::EndPopup();
        }
    } else if (t) {
        if (unfinished_ && unfinished_->dir == t->dir()) {
            if (!drawStartWaiting("resume_match", resumeKey(unfinished_->dir)) &&
                ui::iconButton("resume_match", ui::Icon::Play, TR("Resume")))
                resumeRun();
            ImGui::SameLine();
        }
        if (isMatch()) {
            if (!drawStartWaiting("relaunch", "start")) drawRelaunch(info.engine_a, info.engine_b, t->dir());
            ImGui::SameLine();
        }
        if (ui::iconButton("folder", ui::Icon::Folder, TR("Folder"))) openFolder(t->dir());
    }
    if (run_.running() && t) {
        ImGui::SameLine();
        if (ui::iconButton("folder", ui::Icon::Folder, TR("Folder"))) openFolder(t->dir());
    }
    ImGui::SameLine();
    drawLayoutMenu();
}

void App::drawWorkspace() {
    if (!run_.tournament()) {
        ui::pageTitle(TR("No match"), guideSection());
        ImGui::TextDisabled("%s", TR("Start a new tournament, or open one from the list of tournaments."));
        if (ImGui::Button(TR("Tournaments"))) page_ = Page::Home;
        return;
    }

    drawWorkspaceHeader();
    ImGui::Spacing();

    drawWorkspaceDock();
}

bool App::drawStartFailures(const char* title) {
    const auto* t = run_.tournament();
    if (!t) return false;
    const auto& failures = t->startFailures();
    if (failures.empty()) return false;

    // The participants still playing: one withdrawn or replaced meanwhile is no longer waited for.
    std::vector<tour::StartFailures::Engine> shown;
    for (auto& e : failures.engines()) {
        if (const auto* p = t->spec().participant(e.participant); p && p->active) shown.push_back(std::move(e));
    }
    if (shown.empty()) return false;

    if (title) sectionTitle(title);
    const bool running  = run_.running();
    const bool editable = t->spec().format == tour::Format::RoundRobin || t->spec().format == tour::Format::Gauntlet;
    const auto now      = tour::StartFailures::Clock::now();
    const auto tip      = [](const std::string& text) {
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };

    ui::beginCard("start_failures_card");
    for (std::size_t i = 0; i < shown.size(); ++i) {
        const auto& e = shown[i];
        ImGui::PushID(e.participant);
        if (i > 0) ImGui::Separator();

        auto heading = trf("{} cannot start", e.name);
        if (!e.failed) heading = trf("{} did not start (try {} of {})", e.name, e.tries, tour::StartFailures::TRIES);
        ImGui::PushStyleColor(ImGuiCol_Text, e.failed ? ui::BAD : ui::WARN);
        ImGui::TextUnformatted(heading.c_str());
        ImGui::PopStyleColor();
        // fastchess' words, as the log has them.
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(e.error.c_str());
        ImGui::PopTextWrapPos();

        std::string state;
        if (!running && failures.stalled()) {
            state = TR("No game is left to play without it: the tournament stopped. Resume tries it again.");
        } else if (!running) {
            state = TR("The tournament is stopped: Resume tries it again.");
        } else if (e.trying) {
            state = TR("Trying to start it now…");
        } else {
            const double seconds = std::chrono::duration<double>(e.next_try - now).count();
            const auto wait      = ui::formatDuration(std::ceil(std::max(0.0, seconds)));
            if (e.failed) {
                state = trf("Its games are set aside, the others go on. Next try in {}.", wait);
            } else {
                state = trf("Tried again in {}.", wait);
            }
        }
        ImGui::TextDisabled("%s", state.c_str());

        // On one line, each saying what it does at the cursor.
        bool first        = true;
        const auto button = [&](const char* label, const std::string& tooltip) {
            if (!first) ImGui::SameLine();
            first          = false;
            const bool hit = ImGui::Button(label);
            tip(tooltip);
            return hit;
        };
        if (running) {
            ImGui::BeginDisabled(e.trying || run_.stopping());
            const char* try_tip =
                TR("Try to start it now, rather than at its next try: its games come back if it starts.");
            if (button(TR("Try again"), try_tip) && run_.tryEngineAgain(e.participant)) {
                Console::instance().add(trf("{}: trying to start it again.", e.name));
            }
            ImGui::EndDisabled();
        }
        if (editable) {
            const std::string impact   = TR("The Schedule tab shows the impact before anything changes.");
            const std::string withdraw = TR("Its games left are dropped.");
            const std::string replace =
                TR("Another engine of the list takes its place and plays its games, those it played included.");
            if (button(TR("Withdraw…"), withdraw + " " + impact)) {
                openParticipantEdit(tour::Edit::Kind::Withdraw, e.participant);
            }
            if (button(TR("Replace…"), replace + " " + impact)) {
                openParticipantEdit(tour::Edit::Kind::ReplaceParticipant, e.participant);
            }
        }
        // Set once written.
        if (!e.log.empty()) {
            const auto log_tip = trf(
                "What was sent to the engines of its last try and what they answered, their error output included:\n{}",
                e.log.string());
            if (button(TR("Engine log"), log_tip)) ui::openPath(e.log);
        }
        ImGui::PopID();
    }
    ui::endCard();
    return true;
}

std::vector<Notice> App::engineIncidentNotices(const tour::Incidents& incidents) const {
    // The engines that lost those games, the most first: which engine, what, and its card and engine logs.
    constexpr std::size_t SHOWN = 3;
    std::vector<Notice> out;
    for (const auto& e : incidents.engines) {
        if (out.size() == SHOWN) break;
        std::vector<std::string> parts;
        if (e.crashes > 0) parts.push_back(trn("{} crash or hang", "{} crashes or hangs", e.crashes, e.crashes));
        if (e.early_time_losses > 0) {
            parts.push_back(trn("{} loss on time in the first moves", "{} losses on time in the first moves",
                                e.early_time_losses, e.early_time_losses));
        }
        if (const int late = e.time_losses - e.early_time_losses; late > 0)
            parts.push_back(trn("{} loss on time", "{} losses on time", late, late));
        if (e.illegal_moves > 0)
            parts.push_back(trn("{} illegal move", "{} illegal moves", e.illegal_moves, e.illegal_moves));
        std::string text;
        for (const auto& p : parts) text += (text.empty() ? "" : ", ") + p;

        Notice n{Notice::Level::Warning, trf("{}: {}", e.engine, text), {}, {}};
        if (e.early_time_losses > 0) {
            n.detail =
                trf("Lost on time before move {}: more often an engine set up wrong (too many threads, a hash "
                    "too large to allocate in time, a network loaded at every game, a slow start) than its "
                    "time management.",
                    tour::EARLY_TIME_LOSS_PLIES / 2);
        }
        if (const auto engine = libraryEngine(engines_, e.engine); !engine.empty())
            n.fixes.push_back(openEngineFix(engine));
        auto folder    = openFolderFix(e.dir.string());
        folder.tooltip = fmt::format("{}\n{}", e.dir.string(),
                                     TR("Its logs folder keeps the engine log of every game "
                                        "that ended badly."));
        n.fixes.push_back(std::move(folder));
        out.push_back(std::move(n));
    }
    return out;
}

void App::drawRelaunch(const std::string& engine_a, const std::string& engine_b, const fs::path& from, bool open) {
    if (ui::iconButton("relaunch", ui::Icon::Play, TR("Relaunch with…")) || open) {
        relaunch_engine_ = engine_a;
        ImGui::OpenPopup("relaunch_popup");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "%s", trf("The same match with a new engine A in place of {}, e.g. its next version.", engine_a).c_str());
    }
    if (!ImGui::BeginPopup("relaunch_popup")) return;

    ImGui::TextUnformatted(TR("New engine A"));
    ImGui::SetNextItemWidth(px(260));
    if (ImGui::BeginCombo("##relaunch_engine", relaunch_engine_.c_str(), ImGuiComboFlags_HeightLargest)) {
        std::vector<std::string> names, more;
        for (const auto& e : engines_) {
            if (e.inactive && e.name != relaunch_engine_) continue;
            names.push_back(e.name);
            more.push_back(engineSearchText(e));
        }
        if (const int i = ui::searchComboItems(names, relaunch_engine_, &more); i >= 0) relaunch_engine_ = names[i];
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("%s", trf("against {}", engine_b).c_str());
    ImGui::TextDisabled("%s", TR("The other settings: those of the New tournament page."));
    const bool known =
        std::any_of(engines_.begin(), engines_.end(), [&](const EngineEntry& e) { return e.name == relaunch_engine_; });
    ImGui::BeginDisabled(!known || run_.running());
    if (ImGui::Button(TR("Start"))) {
        setup_.format   = TournamentFormat::Match;
        setup_.engine_a = relaunch_engine_;
        setup_.engine_b = engine_b;
        setup_.name.clear();
        relaunch_from_ = from;
        ImGui::CloseCurrentPopup();
        startRun();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Settings

void App::drawSettings() {
    ui::pageTitle(TR("Settings"), guideSection());
    ImGui::Spacing();

    bool changed      = false;
    const float label = px(220);

    ui::label(TR("Language"), label);
    ImGui::SetNextItemWidth(px(220));
    // Each language under its own name: Chinese too, hence its font.
    ui::requestCjkFont();
    const auto& all = i18n::languages();
    std::vector<const char*> names{TR("Automatic (system)")};
    int language = 0;
    for (std::size_t i = 0; i < all.size(); ++i) {
        names.push_back(all[i].name);
        if (setup_.language == all[i].code) language = static_cast<int>(i) + 1;
    }
    if (ImGui::Combo("##language", &language, names.data(), static_cast<int>(names.size()))) {
        setup_.language = language == 0 ? "auto" : all[static_cast<std::size_t>(language) - 1].code;
        i18n::setLanguage(i18n::resolve(setup_.language));
        changed = true;
    }

    ui::label(TR("Theme"), label);
    ImGui::SetNextItemWidth(px(220));
    const char* themes[] = {TR("Automatic (system)"), TR("Dark"), TR("Light")};
    int theme            = setup_.theme == "dark" ? 1 : setup_.theme == "light" ? 2 : 0;
    if (ImGui::Combo("##theme", &theme, themes, 3)) {
        setup_.theme = theme == 1 ? "dark" : theme == 2 ? "light" : "system";
        changed      = true;
    }

    ui::label(TR("Interface size"), label);
    ImGui::SetNextItemWidth(px(220));
    if (ImGui::BeginCombo("##zoom", fmt::format("{} %", setup_.ui_zoom).c_str())) {
        for (const int zoom : ZOOMS) {
            if (ImGui::Selectable(fmt::format("{} %", zoom).c_str(), zoom == setup_.ui_zoom)) {
                setup_.ui_zoom = zoom;
                changed        = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", TR("Ctrl + mouse wheel, Ctrl + and Ctrl -, Ctrl 0"));

    ImGui::Spacing();
    changed |= ImGui::Checkbox(TR("Notify when a match ends"), &setup_.notify);
    changed |= ImGui::Checkbox(TR("Resume at launch the tournament that was running"), &setup_.resume_at_launch);
    ui::help(
        TR("When the program ended while a tournament was running (a crash, a power cut, a reboot, or quitting "
           "while it ran), it resumes at the next launch, and the queue goes on after it. A tournament paused or "
           "stopped waits for you."));

    // Where the program keeps its files, and whether that is beside the program (the portable mode).
    ImGui::Spacing();
    ImGui::Separator();
    ui::label(TR("Profile"), label);
    ImGui::TextUnformatted(storage_.root().string().c_str());
    ImGui::SetCursorPosX(label);
    if (ImGui::Button(TR("Open the folder##profile"))) openFolder(storage_.root());
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (portable::active()) {
        ImGui::TextUnformatted(TR("Portable mode"));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", trf("The file named \"portable\" beside the program keeps everything in this "
                                        "folder. The paths in {} are kept relative to it, so that folder can be moved "
                                        "or copied to another machine.",
                                        portable::base().string())
                                        .c_str());
        }
    } else {
        ImGui::TextDisabled("%s", TR("Portable mode: off"));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("To keep everything beside the program instead (a USB stick, a folder copied "
                                       "between machines), put an empty file named \"portable\" beside it: the "
                                       "profile is then the folder \"data\" beside it. Copy this folder's files there "
                                       "to keep them."));
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ui::label(TR("Tournaments folder"), label);
    ImGui::TextUnformatted(storage_.runsDir().string().c_str());
    ImGui::SetCursorPosX(label);  // under the path: ui::label() places it at `label` from the window's left
    if (ImGui::Button(TR("Change…"))) {
        dialogs_.openFolder(Dialogs::Purpose::TournamentsFolder, storage_.runsDir().string());
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Open the folder"))) openFolder(storage_.runsDir());
    if (storage_.runsDir() != storage_.defaultRunsDir()) {
        ImGui::SameLine();
        if (ImGui::Button(TR("Back to the default"))) {
            storage_.setRunsDir({});
            tournaments_dirty_ = true;
            game_db_dirty_     = true;
            unfinished_        = unfinishedMatch(storage_.runsDir());
        }
    }
    ImGui::SetCursorPosX(label);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", TR("New tournaments go there, and the lists show the tournaments found there. Tournaments "
                                "already played are not moved: a tournament is a folder, move it yourself."));
    ImGui::PopStyleColor();

    // lichess, for the broadcasts of the Broadcast tab.
    ImGui::Spacing();
    ImGui::Separator();
    ui::label("lichess", label);
    ImGui::BeginGroup();
    ImGui::PushItemWidth(px(420));
    lichessTokenField();
    ImGui::PopItemWidth();
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();
    ui::label(TR("Version"), label);
    ImGui::BeginGroup();
    drawVersionSettings();
    ImGui::EndGroup();
    // What draws the window, and the software renderer for the machines where OpenGL fails.
    ImGui::Spacing();
    ImGui::Separator();
    ui::label(TR("Rendering"), label);
    ImGui::TextUnformatted(render::description().c_str());
    if (!render::fallbackReason().empty()) {
        ImGui::SetCursorPosX(label);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::WARN));
        ImGui::TextWrapped("%s", render::fallbackReason().c_str());
        ImGui::PopStyleColor();
    }
    ImGui::SetCursorPosX(label);
    bool software = storage_.softwareRendering();
    if (ImGui::Checkbox(TR("Software rendering"), &software)) storage_.setSoftwareRendering(software);
    ImGui::SetItemTooltip(
        "%s", TR("The window drawn by the processor (SDL's software renderer) rather than by the graphics card with "
                 "OpenGL: for virtual machines, remote desktops and graphics drivers that fail. Slower, and panels "
                 "dragged out of the window stay inside it. --software on the command line does the same for one "
                 "launch."));
    if (software != software_at_launch_) {
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::WARN), "%s", TR("Applies at the next launch."));
    }

    if (changed) saveSetupSoon();
}

// ---------------------------------------------------------------------------
// Version, and the new ones

void App::copyVersion() {
    ImGui::SetClipboardText(fmt::format("fastchess-gui {} ({})", FCG_VERSION, SDL_GetPlatform()).c_str());
}

void App::startUpdateCheck(bool asked) {
    if (update_checker_.running()) return;
    update_asked_ = asked;
    update_status_.clear();
    update_checker_.start();
}

void App::pollUpdateCheck() {
    const auto answer = update_checker_.poll();
    if (!answer) return;
    // Silent unless asked from Settings: the log tells what happened.
    updates_.checked = std::time(nullptr);
    if (answer->release) {
        updates_.latest = answer->release;
        Console::instance().add(
            trf("Latest version on GitHub: {} (this is {})", answer->release->version, FCG_VERSION));
    } else {
        Console::instance().add(trf("Could not check for a new version: {}", answer->error));
        if (update_asked_) update_status_ = trf("Could not check: {}", answer->error);
    }
    storage_.setUpdateSettings(updates_);
}

void App::drawVersionSettings() {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("fastchess-gui " FCG_VERSION);
    ImGui::SameLine();
    if (ImGui::Button(TR("Copy##version"))) copyVersion();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("The version and the system, for a bug report."));
    ImGui::SameLine();
    reportButton("report_settings");

    if (ImGui::Checkbox(TR("Tell me when a new version is out"), &updates_.enabled)) {
        storage_.setUpdateSettings(updates_);
        if (updates_.enabled && update::checkDue(updates_.checked, std::time(nullptr))) startUpdateCheck(true);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("At launch, at most once a day, the program asks GitHub for its latest release; the "
                                   "home page tells when there is a newer one, with its notes and a link to its page. "
                                   "Nothing is downloaded nor installed."));
    }
    if (!updates_.enabled) return;

    if (update_checker_.running()) {
        ImGui::TextDisabled("%s", TR("Checking…"));
    } else if (!update_status_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        ImGui::TextWrapped("%s", update_status_.c_str());
        ImGui::PopStyleColor();
    } else if (updates_.latest && update::compareVersions(updates_.latest->version, FCG_VERSION) > 0) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(trf("Version {} is out.", updates_.latest->version).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Download page"))) SDL_OpenURL(updates_.latest->url.c_str());
    } else if (updates_.latest) {
        ImGui::TextDisabled("%s", TR("This is the latest version."));
    }
}

// ---------------------------------------------------------------------------
// Report a problem

std::string App::systemReport() const {
    // For the maintainers, in English.
    const char* video = SDL_GetCurrentVideoDriver();
    std::string out   = "System: " + report::systemName();
    out += fmt::format("\nDisplay: {}, scale {:.2f}, interface {} %", video ? video : "?", options_.ui_scale,
                       setup_.ui_zoom);
    out += fmt::format("\nProcessor: {} physical cores, {} threads", topology_.physical, topology_.logical);
    if (const auto memory = systemMemory()) out += "\nMemory: " + formatMemory(memory->total_mb);
    out += "\nGraphics: " + (gl::driver().empty() ? render::description() : gl::driver());
    out += fmt::format("\nLanguage: {}", i18n::info(i18n::language()).code);
    return out;
}

void App::reportProblem(const std::string& seen) {
    report::Facts facts;
    facts.version  = "fastchess-gui " FCG_VERSION;
    facts.system   = systemReport();
    facts.log      = report::lastLines(storage_.root() / "fastchess-gui.log", 80);
    facts.seen     = seen;
    const auto url = report::issueUrl(facts);
    if (!SDL_OpenURL(url.c_str())) {
        ImGui::SetClipboardText(url.c_str());
        Console::instance().add(
            TR("The browser could not be opened: the address of the new issue is on the clipboard."), true);
    }
}

bool App::reportButton(const char* id, bool small, const std::string& seen) {
    ImGui::PushID(id);
    const bool pressed = small ? ImGui::SmallButton(TR("Report a problem…")) : ImGui::Button(TR("Report a problem…"));
    ImGui::PopID();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("Opens a new issue on GitHub in the browser, filled with the version, the system, "
                                   "the graphics driver and the last lines of the log. Read it and complete it before "
                                   "sending it: the program sends nothing itself."));
    }
    if (pressed) reportProblem(seen);
    return pressed;
}

// On the home page, while a newer version than this one is out and the user did not close its notice.
void App::drawUpdateNotice() {
    const auto* release = update::notice(updates_, FCG_VERSION);
    if (!release) return;
    ui::beginCard("update_notice");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(trf("fastchess-gui {} is out (this is {}).", release->version, FCG_VERSION).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Download page"))) SDL_OpenURL(release->url.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", release->url.c_str());
    if (!release->notes.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("What's new"))) update_notes_open_ = !update_notes_open_;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Hide##update"))) {
        updates_.dismissed = release->version;
        storage_.setUpdateSettings(updates_);
        ui::endCard();
        return;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Until the next version."));
    if (update_notes_open_ && !release->notes.empty()) {
        // The notes as written (Markdown), in a box of their own.
        ImGui::BeginChild("##update_notes", ImVec2(0, px(220)), ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(release->notes.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
    }
    ui::endCard();
}

}  // namespace fcg
