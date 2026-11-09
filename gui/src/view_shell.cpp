// The application shell: navigation rail and pages (home, new match, tournament workspace, engines, log,
// settings). The tournament views themselves live in view_*.cpp and draw inside these pages.

#include <algorithm>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <tour/legacy.hpp>

#include "app.hpp"
#include "console.hpp"
#include "i18n.hpp"
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

void openFolder(const fs::path& dir) {
    const auto url = "file://" + dir.generic_string();
    SDL_OpenURL(url.c_str());
}

void sectionTitle(const char* text) {
    ImGui::Spacing();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

}  // namespace

void App::applyThemeSetting() {
    ui::Theme wanted = ui::Theme::Dark;
    if (setup_.theme == "light") {
        wanted = ui::Theme::Light;
    } else if (setup_.theme != "dark") {
        wanted = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_LIGHT ? ui::Theme::Light : ui::Theme::Dark;
    }
    if (theme_applied_ == wanted) return;

    ui::applyTheme(wanted, options_.ui_scale);
    theme_applied_ = wanted;
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

    // The logo: "fc" on the accent colour.
    {
        ImDrawList* dl   = ImGui::GetWindowDrawList();
        const auto pos   = ImGui::GetCursorScreenPos();
        const float size = px(36);
        const ImVec2 min(pos.x + (width - size) * 0.5f, pos.y);
        dl->AddRectFilled(min, ImVec2(min.x + size, min.y + size), ui::ACCENT, px(10));
        const auto ts = ImGui::CalcTextSize("fc");
        dl->AddText(ImVec2(min.x + (size - ts.x) * 0.5f, min.y + (size - ts.y) * 0.5f),
                    ui::currentTheme() == ui::Theme::Dark ? IM_COL32(11, 18, 32, 255) : IM_COL32_WHITE, "fc");
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
    if (ui::railButton("rail_log", ui::Icon::Log, TR("Log"), page_ == Page::Log, width)) page_ = Page::Log;
    if (ui::railButton("rail_settings", ui::Icon::Settings, TR("Settings"), page_ == Page::Settings, width)) {
        page_ = Page::Settings;
    }

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
    if (ui::iconButton("new_match", ui::Icon::Plus, TR("New match"))) page_ = Page::NewMatch;
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
        ImGui::TextDisabled("%s", TR("No tournament yet. Start with a new match."));
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

    // The form in one readable column, the summary beside it when there is room.
    const float avail   = ImGui::GetContentRegionAvail().x;
    const float summary = px(330);
    const bool side     = avail > px(760) + summary;
    const float width   = side ? std::min(px(760), avail - summary - px(20)) : avail;

    ImGui::BeginChild("##new_match", ImVec2(width, 0));
    if (!side) drawSetupSummary();
    drawSetup();
    ImGui::EndChild();

    if (side) {
        ImGui::SameLine(0, px(20));
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
        ImGui::TextDisabled("%s", TR("Start a new match, or open one from the list of tournaments."));
        if (ImGui::Button(TR("Tournaments"))) page_ = Page::Home;
        return;
    }

    drawWorkspaceHeader();
    ImGui::Spacing();

    if (!ImGui::BeginTabBar("workspace_tabs")) return;

    const auto tab = [&](const char* label, Tab which) {
        ImGuiTabItemFlags flags = 0;
        if (tab_request_ == which) {
            flags        = ImGuiTabItemFlags_SetSelected;
            tab_request_ = std::nullopt;
        }
        return ImGui::BeginTabItem(label, nullptr, flags);
    };

    if (tab(TR("Overview###tab_overview"), Tab::Overview)) {
        if (isMatch()) {
            const float results_h = std::max(px(150), ImGui::GetContentRegionAvail().y * 0.30f);
            ImGui::BeginChild("##overview_results", ImVec2(0, results_h));
            drawResults();
            ImGui::EndChild();
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
    if (!isMatch() && tab(TR("Schedule###tab_schedule"), Tab::Schedule)) {
        drawSchedule();
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
    ImGui::EndTabBar();
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
    const char* languages[] = {TR("Automatic (system)"), "Français", "English"};
    int language            = setup_.language == "fr" ? 1 : setup_.language == "en" ? 2 : 0;
    if (ImGui::Combo("##language", &language, languages, 3)) {
        setup_.language = language == 1 ? "fr" : language == 2 ? "en" : "auto";
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

    ImGui::Spacing();
    changed |= ImGui::Checkbox(TR("Notify when a match ends"), &setup_.notify);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("%s", trf("Tournaments are stored in {}", storage_.runsDir().string()).c_str());
    if (ImGui::Button(TR("Open the folder"))) openFolder(storage_.runsDir());

    if (changed) saveSetupSoon();
}

}  // namespace fcg
