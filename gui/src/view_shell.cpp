// The application shell: navigation rail and pages (home, new match, tournament workspace, engines, log,
// settings). The tournament views themselves live in view_*.cpp and draw inside these pages.

#include <algorithm>
#include <array>
#include <ctime>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <tour/legacy.hpp>

#include "app.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "logo.hpp"
#include "memory.hpp"
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

// When something `seconds` from now ends, in local time: "18:40" today, "2026-11-14 03:10" another day.
std::string endTime(double seconds) {
    const auto now = std::time(nullptr);
    const auto end = now + static_cast<std::time_t>(seconds);
    std::tm today{}, then{};
#ifdef _WIN32
    localtime_s(&today, &now);
    localtime_s(&then, &end);
#else
    localtime_r(&now, &today);
    localtime_r(&end, &then);
#endif
    char text[32];
    const bool same_day = today.tm_year == then.tm_year && today.tm_yday == then.tm_yday;
    std::strftime(text, sizeof(text), same_day ? "%H:%M" : "%Y-%m-%d %H:%M", &then);
    return text;
}

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
    // Eight buttons under the logo: lower ones in a low window, rather than a scroll bar.
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    const float button  = std::clamp((ImGui::GetContentRegionAvail().y - px(48) - 8 * spacing) / 8,
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
// Home

void App::drawHome() {
    refreshTournaments();

    const float buttons = px(330);
    ui::pageTitle(TR("Tournaments"), guideSection());
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
        if (const auto left = runSecondsLeft()) {
            ImGui::TextDisabled("%s", trf("Ends about {} (in {})", endTime(*left), ui::formatDuration(*left)).c_str());
        }
        if (ImGui::Button(TR("Open"))) page_ = Page::Tournament;
        ui::endCard();
    } else if (unfinished_) {
        sectionTitle(TR("Interrupted"));
        ui::beginCard("unfinished_card");
        const auto& u = *unfinished_;
        ImGui::TextUnformatted(u.spec.name.c_str());
        ImGui::TextDisabled(
            "%s", (u.total ? trf("{} / {} games", u.played, *u.total) : trn("{} game", "{} games", u.played, u.played))
                      .c_str());
        if (!resume_crashed_.empty() && fs::absolute(u.dir).lexically_normal() == resume_crashed_) {
            ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(TR("The program ended while it resumed this tournament at the last launch: it is "
                                      "not resumed automatically again."));
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        if (ui::iconButton("resume_match", ui::Icon::Play, TR("Resume"))) resumeRun();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("It continues exactly where it stopped: no game is lost or played twice."));
        ui::endCard();
    }

    drawQueue();
    drawHealth();

    if (legacy_runs_ > 0) {
        ImGui::Spacing();
        ui::beginCard("legacy_card");
        ImGui::TextUnformatted(trn("{} match of an earlier version is not in the list yet.",
                                   "{} matches of an earlier version are not in the list yet.", legacy_runs_,
                                   legacy_runs_)
                                   .c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Import them"))) importLegacyRuns();
        ui::endCard();
    }

    sectionTitle(TR("All tournaments"));
    if (tournaments_.empty()) {
        if (tournaments_read_) ImGui::TextDisabled("%s", TR("No tournament yet. Start with a new tournament."));
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
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ui::currentTheme() == ui::Theme::Dark ? ImVec4(0.04f, 0.07f, 0.13f, 1) : ImVec4(1, 1, 1, 1));
    if (ui::iconButton("start", ui::Icon::Play, TR("Start"), ready && !probing && !locked && !start_after_reads_))
        startRun();
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
    ImGui::SameLine();
    if (ui::iconButton("enqueue", ui::Icon::Plus, TR("Add to the queue"), ready)) enqueue();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", bad_sprt ? TR("elo1 must be larger than elo0.")
                                         : TR("Starts by itself when the running tournament ends."));
    }
    if (!start_error_.empty()) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::TextWrapped("%s", start_error_.c_str());
        ImGui::PopStyleColor();
    }
    if (start_after_reads_) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", TR("Starts once the engine being read or checked is done…"));
        ImGui::SameLine();
        if (ImGui::SmallButton(TR("Cancel##start_after_reads"))) start_after_reads_ = {};
    }
    ImGui::Spacing();

    // The summary on the right, the form beside it: in two columns when both are wide enough to read, centred when
    // even two columns leave room. A narrow window gets one column, under the summary, in the same scroll.
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
    drawSetup(two, !side);
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
                               : info.total_games ? trn("{} game", "{} games", info.total_games, info.total_games)
                                                  : std::string(TR("until stopped")))
                            : trf("{} engines · {} games", info.participants, info.total_games);
    ImGui::TextDisabled("%s", fmt::format("{} · {} · {}", formatLabel(info.format), info.limit_text, detail).c_str());
    ui::pageTitle(t ? t->spec().name.c_str() : TR("No match"), guideSection());

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

void App::drawQueue() {
    if (queue_.empty()) return;

    sectionTitle(TR("Queue"));
    ui::beginCard("queue_card");
    ImGui::TextDisabled("%s", TR("Started one after the other, when the running tournament ends by itself."));

    // When it is all over: the running tournament at its pace, the queued ones from their games and time control.
    double seconds = 0;
    int unknown    = 0;
    if (run_.running()) {
        if (const auto left = runSecondsLeft())
            seconds += *left;
        else
            ++unknown;
    }
    for (const auto& q : queue_) {
        if (const auto s = plannedSeconds(q.setup, topology_))
            seconds += *s;
        else
            ++unknown;
    }
    const bool everything = unknown == static_cast<int>(queue_.size()) + (run_.running() ? 1 : 0);
    if (!everything) {
        auto text = trf("The queue ends about {} (in {})", endTime(seconds), ui::formatDuration(seconds));
        if (unknown > 0) {
            text += trn(", and {} tournament of unknown length", ", and {} tournaments of unknown length", unknown,
                        unknown);
        }
        ImGui::TextUnformatted(text.c_str());
    }

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

std::optional<double> App::runSecondsLeft() const {
    const auto total = run_.info().total_games;
    if (!run_.running() || total == 0 || games_per_hour_ <= 0) return std::nullopt;
    const auto played = static_cast<uint64_t>(games_.size());
    return played >= total ? 0.0 : static_cast<double>(total - played) / games_per_hour_ * 3600.0;
}

void App::drawHealth() {
    // The journals of the last 24 hours, read again every 30 seconds on a worker thread: only what they gained.
    const auto now = std::chrono::steady_clock::now();
    if (incidents_job_.valid() && incidents_job_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        incidents_ = incidents_job_.get();
    if (!incidents_job_.valid() && now - incidents_asked_ > std::chrono::seconds(30)) {
        incidents_asked_ = now;
        const auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
                               (std::chrono::system_clock::now() - std::chrono::hours(24)).time_since_epoch())
                               .count();
        incidents_job_   = std::async(std::launch::async, [scanner = incident_scanner_, root = storage_.runsDir(),
                                                           since] { return scanner->scan(root, since); });
    }

    sectionTitle(TR("Health"));
    ui::beginCard("health_card");
    const auto item = [&](bool warn, const std::string& text, bool first = false) {
        if (!first) {
            ImGui::SameLine();
            ImGui::TextDisabled("·");
            ImGui::SameLine();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, warn ? ui::WARN : ImGui::GetColorU32(ImGuiCol_Text));
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();
    };

    ImGui::TextDisabled("%s", TR("Last 24 hours:"));
    ImGui::SameLine();
    if (incidents_) {
        const auto& i = *incidents_;
        item(i.crashes > 0, trn("{} crash or hang", "{} crashes or hangs", i.crashes, i.crashes), true);
        item(i.time_losses > 0, trn("{} loss on time", "{} losses on time", i.time_losses, i.time_losses));
        item(i.illegal_moves > 0, trn("{} illegal move", "{} illegal moves", i.illegal_moves, i.illegal_moves));
    } else {
        ImGui::TextDisabled("%s", TR("reading the tournaments…"));
    }

    // Free memory below a tenth of the installed is worth a look.
    if (const auto memory = systemMemory()) {
        item(memory->available_mb * 10 < memory->total_mb,
             trf("{} of memory free of {}", formatMemory(memory->available_mb), formatMemory(memory->total_mb)), true);
    }
    const int processes = engineProcesses();
    item(false, trn("{} engine process running", "{} engine processes running", processes, processes),
         !systemMemory().has_value());
    ui::endCard();
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
