// The application shell: navigation rail and pages (home, new match, tournament workspace, engines, log,
// settings). The tournament views themselves live in view_*.cpp and draw inside these pages.

#include <algorithm>
#include <array>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <tour/legacy.hpp>

#include "app.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "logo.hpp"
#include "ui.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// "20261109-185927_a_vs_b" -> "2026-11-09 18:59"
std::string folderDate(const fs::path& dir) {
    const auto name = dir.filename().string();
    if (name.size() < 15 || name[8] != '-') return "";
    return name.substr(0, 4) + "-" + name.substr(4, 2) + "-" + name.substr(6, 2) + " " + name.substr(9, 2) + ":" +
           name.substr(11, 2);
}

std::string formatLabel(tour::Format format) {
    switch (format) {
        case tour::Format::Match:
            return TR("Match");
        case tour::Format::RoundRobin:
            return TR("Round robin");
        case tour::Format::Gauntlet:
            return TR("Gauntlet");
        case tour::Format::Swiss:
            return TR("Swiss");
        case tour::Format::Knockout:
            return TR("Knockout");
    }
    return "";
}

std::string statusLabel(const tour::Summary& s, bool running) {
    if (running) return TR("Running");
    if (!s.finished) return TR("Interrupted");
    if (s.finish_reason == "sprt_h1") return TR("H1 accepted");
    if (s.finish_reason == "sprt_h0") return TR("H0 accepted");
    if (s.finish_reason == "imported") return TR("Imported");
    return TR("Finished");
}

void openFolder(const fs::path& dir) { ui::openPath(dir); }

// The interface sizes offered, in percent.
constexpr std::array<int, 9> ZOOMS = {80, 90, 100, 110, 125, 150, 175, 200, 250};

void sectionTitle(const char* text) {
    ImGui::Spacing();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

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

// Ctrl + and Ctrl -, or Ctrl and the mouse wheel, step through the sizes; Ctrl 0 goes back to 100 %.
void App::handleZoomKeys() {
    const auto& io = ImGui::GetIO();
    if (!io.KeyCtrl) return;
    int step = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) step = 1;
    if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) step = -1;
    if (io.MouseWheel != 0 && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) step = io.MouseWheel > 0 ? 1 : -1;

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
    tournaments_       = tour::listTournaments(storage_.runsDir());
    legacy_runs_       = tour::legacyRuns(storage_.runsDir()).size();
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
    Console::instance().add(trf("{} earlier matches imported.", imported));
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
            ui::pageTitle(TR("Engines"));
            drawEngines();
            break;
        case Page::Books:
            ui::pageTitle(TR("Books"));
            drawBooks();
            break;
        case Page::Suites:
            ui::pageTitle(TR("Test suites"));
            drawSuites();
            break;
        case Page::Ratings:
            drawRatings();
            break;
        case Page::Log:
            ui::pageTitle(TR("Log"));
            drawConsole();
            break;
        case Page::Settings:
            drawSettings();
            break;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();
}

void App::drawRail(float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::RAIL_BG));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, px(14)));
    ImGui::BeginChild("##rail", ImVec2(width, 0), ImGuiChildFlags_AlwaysUseWindowPadding);

    // The logo, on the accent colour.
    {
        const auto pos   = ImGui::GetCursorScreenPos();
        const float size = px(36);
        logo::draw(ImGui::GetWindowDrawList(), ImVec2(pos.x + (width - size) * 0.5f, pos.y), size, ui::ACCENT,
                   ui::currentTheme() == ui::Theme::Dark ? IM_COL32(11, 18, 32, 255) : IM_COL32_WHITE);
        ImGui::Dummy(ImVec2(width, size + px(12)));
    }

    const bool in_tournaments = page_ == Page::Home || page_ == Page::NewMatch || page_ == Page::Tournament;
    if (ui::railButton("rail_home", ui::Icon::Trophy, TR("Tournaments"), in_tournaments, width)) {
        page_              = Page::Home;
        tournaments_dirty_ = true;
    }
    if (ui::railButton("rail_engines", ui::Icon::Engine, TR("Engines"), page_ == Page::Engines, width)) {
        page_ = Page::Engines;
    }
    if (ui::railButton("rail_books", ui::Icon::Book, TR("Books"), page_ == Page::Books, width)) page_ = Page::Books;
    if (ui::railButton("rail_suites", ui::Icon::Suite, TR("Suites"), page_ == Page::Suites, width)) {
        page_ = Page::Suites;
    }
    if (ui::railButton("rail_ratings", ui::Icon::Chart, TR("Ratings"), page_ == Page::Ratings, width)) {
        page_          = Page::Ratings;
        game_db_dirty_ = true;
    }
    if (ui::railButton("rail_log", ui::Icon::Log, TR("Log"), page_ == Page::Log, width)) page_ = Page::Log;
    if (ui::railButton("rail_settings", ui::Icon::Settings, TR("Settings"), page_ == Page::Settings, width)) {
        page_ = Page::Settings;
    }

    // Quit, at the bottom: the window's own close button is not always in reach.
    const float quit_h = ImGui::GetFontSize() * 3.4f;
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - quit_h - px(14)));
    if (ui::railButton("rail_quit", ui::Icon::Quit, TR("Quit"), false, width)) requestQuit();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Home

void App::drawHome() {
    if (tournaments_dirty_) refreshTournaments();

    const float buttons = px(330);
    ui::pageTitle(TR("Tournaments"));
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(20), ImGui::GetContentRegionMax().x - buttons));
    if (ui::iconButton("open_folder", ui::Icon::Folder, TR("Open a folder…"))) {
        dialogs_.openFolder(Dialogs::Purpose::OpenTournament, storage_.runsDir().string());
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ui::currentTheme() == ui::Theme::Dark ? ImVec4(0.04f, 0.07f, 0.13f, 1) : ImVec4(1, 1, 1, 1));
    if (ui::iconButton("new_match", ui::Icon::Plus, TR("New tournament"))) page_ = Page::NewMatch;
    ImGui::PopStyleColor(2);

    ImGui::TextDisabled(
        "%s", TR("Every tournament is a folder: it resumes exactly where it stopped, even after a power cut."));

    if (!start_error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", start_error_.c_str());
        ImGui::PopStyleColor();
    }

    // What is going on now.
    if (run_.running()) {
        sectionTitle(TR("Running"));
        ui::beginCard("running_card");
        const auto* t = run_.tournament();
        ImGui::TextUnformatted(t ? t->spec().name.c_str() : "");
        const auto& info = run_.info();
        if (info.total_games > 0) {
            ImGui::ProgressBar(static_cast<float>(games_.size()) / static_cast<float>(info.total_games),
                               ImVec2(-FLT_MIN, px(8)), "");
        }
        ImGui::TextDisabled("%s", runStatusText().c_str());
        if (ImGui::Button(TR("Open"))) page_ = Page::Tournament;
        ui::endCard();
    } else if (unfinished_) {
        sectionTitle(TR("Interrupted"));
        ui::beginCard("unfinished_card");
        const auto& u = *unfinished_;
        ImGui::TextUnformatted(u.spec.name.c_str());
        ImGui::TextDisabled("%s",
                            (u.total ? trf("{} / {} games", u.played, *u.total) : trf("{} games", u.played)).c_str());
        if (ui::iconButton("resume_match", ui::Icon::Play, TR("Resume"))) resumeRun();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("It continues exactly where it stopped: no game is lost or played twice."));
        ui::endCard();
    }

    drawQueue();

    if (legacy_runs_ > 0) {
        ImGui::Spacing();
        ui::beginCard("legacy_card");
        ImGui::TextUnformatted(trf("{} matches of an earlier version are not in the list yet.", legacy_runs_).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Import them"))) importLegacyRuns();
        ui::endCard();
    }

    sectionTitle(TR("All tournaments"));
    if (tournaments_.empty()) {
        ImGui::TextDisabled("%s", TR("No tournament yet. Start with a new tournament."));
        return;
    }

    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                           ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
    if (!ImGui::BeginTable("tournaments", 6, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(TR("Tournament"), ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn(TR("Format"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(TR("Games"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(TR("State"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn(TR("Date"), ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, px(180));
    ImGui::TableHeadersRow();

    const auto* running = run_.running() ? run_.tournament() : nullptr;
    for (std::size_t i = 0; i < tournaments_.size(); ++i) {
        const auto& s         = tournaments_[i];
        const bool is_running = running && running->dir() == s.dir;
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(s.spec.name.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(formatLabel(s.spec.format).c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(
            (s.total ? fmt::format("{} / {}", s.played, *s.total) : fmt::format("{}", s.played)).c_str());
        ImGui::TableNextColumn();
        const auto status = statusLabel(s, is_running);
        const ImU32 col   = is_running ? ui::ACCENT : !s.finished ? ui::WARN : ui::MUTED;
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(status.c_str());
        ImGui::PopStyleColor();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", folderDate(s.dir).c_str());
        ImGui::TableNextColumn();
        if (ImGui::SmallButton(TR("Open"))) openTournament(s.dir);
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Folder"))) openFolder(s.dir);

        ImGui::PopID();
    }
    ImGui::EndTable();
}

// ---------------------------------------------------------------------------
// New match

void App::drawNewMatch() {
    const bool locked = run_.running();

    ui::pageTitle(TR("New tournament"));
    ImGui::TextDisabled("%s", TR("A match between two versions, a round robin or a gauntlet."));

    const bool ready   = setup_.format == TournamentFormat::Match ? !setup_.engine_a.empty() && !setup_.engine_b.empty()
                                                                  : setup_.participants.size() >= 2;
    const bool probing = probe_.valid() || !probe_queue_.empty();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ui::currentTheme() == ui::Theme::Dark ? ImVec4(0.04f, 0.07f, 0.13f, 1) : ImVec4(1, 1, 1, 1));
    if (ui::iconButton("start", ui::Icon::Play, TR("Start"), ready && !probing && !locked)) startRun();
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    if (ui::iconButton("enqueue", ui::Icon::Plus, TR("Add to the queue"), ready)) enqueue();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", TR("Starts by itself when the running tournament ends."));
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        if (locked)
            ImGui::SetTooltip("%s", TR("A match is already running."));
        else if (!ready)
            ImGui::SetTooltip("%s", setup_.format == TournamentFormat::Match ? TR("Choose engines A and B.")
                                                                             : TR("Choose at least two engines."));
        else if (probing)
            ImGui::SetTooltip("%s", TR("Reading the options of an engine…"));
    }
    if (!start_error_.empty()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", start_error_.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();

    // The summary on the right, the form beside it: in two columns when both are wide enough to read, centred when
    // even two columns leave room. A narrow window gets one column, under the summary.
    const float avail   = ImGui::GetContentRegionAvail().x;
    const float gap     = px(20);
    const float column  = px(760);
    const float summary = std::clamp(avail * 0.24f, px(300), px(400));
    const float rest    = avail - summary - gap;
    const bool side     = rest >= px(560);
    const bool two      = side && rest >= 2 * px(480) + ImGui::GetStyle().ItemSpacing.x;
    const float width   = side ? std::min(rest, two ? 2 * column + ImGui::GetStyle().ItemSpacing.x : column) : avail;
    if (side) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::floor((rest - width) * 0.5f));

    ImGui::BeginChild("##new_match", ImVec2(width, 0));
    if (!side) drawSetupSummary();
    drawSetup(two);
    if (!side) {
        ImGui::Spacing();
        drawTemplates();
    }
    ImGui::EndChild();

    if (side) {
        ImGui::SameLine(0, gap);
        ImGui::BeginChild("##new_summary", ImVec2(summary, 0));
        drawSetupSummary();
        ImGui::Spacing();
        drawTemplates();
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
                               : info.total_games ? trf("{} games", info.total_games)
                                                  : std::string(TR("until stopped")))
                            : trf("{} engines · {} games", info.participants, info.total_games);
    ImGui::TextDisabled("%s", fmt::format("{} · {} · {}", formatLabel(info.format), info.limit_text, detail).c_str());
    ui::pageTitle(t ? t->spec().name.c_str() : TR("No match"));

    const auto status = runStatusText();
    if (run_.state() == RunController::State::Finished && !run_.error().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", status.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::TextUnformatted(status.c_str());
    }

    if (info.total_games > 0 && t) {
        ImGui::ProgressBar(std::min(1.0f, static_cast<float>(games_.size()) / static_cast<float>(info.total_games)),
                           ImVec2(-FLT_MIN, px(6)), "");
    }

    if (run_.running()) {
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

        ImGui::SameLine();
        if (ui::iconButton("stop", ui::Icon::Stop, TR("Stop"), !run_.stopping())) ImGui::OpenPopup("stop_menu");
        if (ImGui::BeginPopup("stop_menu")) {
            if (ImGui::MenuItem(TR("Finish the running games, then stop"))) run_.stopWhenIdle();
            if (ImGui::MenuItem(TR("Stop now (running games are lost)"))) run_.stopNow();
            ImGui::EndPopup();
        }
    } else if (t) {
        if (unfinished_ && unfinished_->dir == t->dir()) {
            if (ui::iconButton("resume_match", ui::Icon::Play, TR("Resume"))) resumeRun();
            ImGui::SameLine();
        }
        if (isMatch()) {
            drawRelaunch();
            ImGui::SameLine();
        }
        if (ui::iconButton("folder", ui::Icon::Folder, TR("Folder"))) openFolder(t->dir());
    }
    if (run_.running() && t) {
        ImGui::SameLine();
        if (ui::iconButton("folder", ui::Icon::Folder, TR("Folder"))) openFolder(t->dir());
    }
}

void App::drawWorkspace() {
    if (!run_.tournament()) {
        ui::pageTitle(TR("No match"));
        ImGui::TextDisabled("%s", TR("Start a new tournament, or open one from the list of tournaments."));
        if (ImGui::Button(TR("Tournaments"))) page_ = Page::Home;
        return;
    }

    drawWorkspaceHeader();
    ImGui::Spacing();

    if (!ImGui::BeginTabBar("workspace_tabs")) return;

    // A requested tab is asked for until it shows: a tab bar's first frame can ignore it.
    const auto tab = [&](const char* label, Tab which) {
        const bool requested = tab_request_ == which;
        const bool open      = ImGui::BeginTabItem(label, nullptr, requested ? ImGuiTabItemFlags_SetSelected : 0);
        if (requested && open) tab_request_ = std::nullopt;
        return open;
    };

    if (tab(TR("Overview###tab_overview"), Tab::Overview)) {
        if (isMatch()) {
            drawResults();
            drawCharts();
        } else {
            drawTournamentOverview();
        }
        ImGui::EndTabItem();
    }
    if (!isMatch() && tab(TR("Standings###tab_standings"), Tab::Standings)) {
        drawStandings();
        ImGui::EndTabItem();
    }
    if (run_.tournament()->spec().format == tour::Format::Knockout && tab(TR("Bracket###tab_bracket"), Tab::Bracket)) {
        drawBracket();
        ImGui::EndTabItem();
    }
    if (!isMatch() && tab(TR("Schedule###tab_schedule"), Tab::Schedule)) {
        drawSchedule();
        ImGui::EndTabItem();
    }
    if (tab(TR("Broadcast###tab_broadcast"), Tab::Broadcast)) {
        drawBroadcast();
        ImGui::EndTabItem();
    }
    if (tab(TR("Live###tab_live"), Tab::Live)) {
        drawLive();
        ImGui::EndTabItem();
    }
    if (tab(TR("Games###tab_games"), Tab::Games)) {
        drawGames();
        ImGui::EndTabItem();
    }
    if (tab(TR("Openings###tab_openings"), Tab::Openings)) {
        drawOpenings();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void App::drawQueue() {
    if (queue_.empty()) return;

    sectionTitle(TR("Queue"));
    ui::beginCard("queue_card");
    ImGui::TextDisabled("%s", TR("Started one after the other, when the running tournament ends by itself."));
    int remove = -1, up = -1;
    for (std::size_t i = 0; i < queue_.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextDisabled("%d", static_cast<int>(i + 1));
        ImGui::SameLine();
        ImGui::TextUnformatted(queue_[i].label.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", formatLabel(static_cast<tour::Format>(queue_[i].setup.format)).c_str());
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + px(10),
                                 ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - px(200)));
        ImGui::BeginDisabled(i == 0);
        if (ImGui::SmallButton("^")) up = static_cast<int>(i);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Remove"))) remove = static_cast<int>(i);
        if (i == 0 && !run_.running()) {
            ImGui::SameLine();
            if (ImGui::SmallButton(TR("Start now"))) {
                ImGui::PopID();
                startFromQueue();
                ui::endCard();
                return;
            }
        }
        ImGui::PopID();
    }
    ui::endCard();

    if (remove >= 0) queue_.erase(queue_.begin() + remove);
    if (up > 0) std::swap(queue_[up], queue_[up - 1]);
    if (remove >= 0 || up > 0) storage_.saveQueue(queue_);
}

void App::drawRelaunch() {
    if (ui::iconButton("relaunch", ui::Icon::Play, TR("Relaunch with…"))) {
        relaunch_engine_ = run_.info().engine_a;
        ImGui::OpenPopup("relaunch_popup");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("The same match with a new engine A, e.g. the next version: every other setting "
                                   "stays."));
    }
    if (!ImGui::BeginPopup("relaunch_popup")) return;

    ImGui::TextUnformatted(TR("New engine A"));
    ImGui::SetNextItemWidth(px(260));
    if (ImGui::BeginCombo("##relaunch_engine", relaunch_engine_.c_str())) {
        for (const auto& e : engines_) {
            if (ImGui::Selectable(e.name.c_str(), e.name == relaunch_engine_)) relaunch_engine_ = e.name;
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("%s", trf("against {}", run_.info().engine_b).c_str());
    const bool known =
        std::any_of(engines_.begin(), engines_.end(), [&](const EngineEntry& e) { return e.name == relaunch_engine_; });
    ImGui::BeginDisabled(!known || run_.running());
    if (ImGui::Button(TR("Start"))) {
        setup_.format   = TournamentFormat::Match;
        setup_.engine_a = relaunch_engine_;
        setup_.engine_b = run_.info().engine_b;
        setup_.name.clear();
        ImGui::CloseCurrentPopup();
        startRun();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Settings

void App::drawSettings() {
    ui::pageTitle(TR("Settings"));
    ImGui::Spacing();

    bool changed      = false;
    const float label = px(220);

    ui::label(TR("Language"), label);
    ImGui::SetNextItemWidth(px(220));
    // Each language under its own name.
    const char* languages[] = {TR("Automatic (system)"), "Deutsch", "English", "Français"};
    const char* codes[]     = {"auto", "de", "en", "fr"};
    int language            = 0;
    for (int i = 1; i < 4; ++i) {
        if (setup_.language == codes[i]) language = i;
    }
    if (ImGui::Combo("##language", &language, languages, 4)) {
        setup_.language = codes[language];
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

    if (changed) saveSetupSoon();
}

}  // namespace fcg
