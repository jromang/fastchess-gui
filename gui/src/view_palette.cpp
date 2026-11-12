// The command palette (Ctrl+K): a page, a tab, an engine, a book, a tournament or an action, found by typing a few of
// its letters. And F1: the guide at the section of the page shown, as the `?` beside the page titles.

#include <algorithm>
#include <string_view>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "i18n.hpp"
#include "palette.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// What a label shows, without its "##" or "###" id.
std::string shown(const char* label) {
    const std::string_view text(label);
    return std::string(text.substr(0, text.find("##")));
}

}  // namespace

guide::Section App::guideSection() const {
    switch (page_) {
        case Page::Home:
            return guide::Section::Running;
        case Page::NewMatch:
            return guide::Section::NewTournament;
        case Page::Tournament:
            if (!run_.tournament()) return guide::Section::Running;
            return shown_tab_ == Tab::Broadcast  ? guide::Section::Broadcast
                   : shown_tab_ == Tab::Schedule ? guide::Section::Changing
                                                 : guide::Section::Running;
        case Page::Engines:
            return guide::Section::Engines;
        case Page::Books:
            return guide::Section::Books;
        case Page::Suites:
            return guide::Section::TestSuites;
        case Page::Ratings:
            return guide::Section::Ratings;
        case Page::Log:
        case Page::Settings:
            break;
    }
    return guide::Section::Window;
}

// The actions possible now first, then the pages and their tabs, the tournaments, the engines and the books.
std::vector<App::PaletteEntry> App::paletteEntries() {
    std::vector<PaletteEntry> out;
    const auto add = [&](std::string label, std::string where, std::function<void()> run) {
        out.push_back({std::move(label), std::move(where), std::move(run)});
    };
    const auto* t          = run_.tournament();
    const std::string name = t ? t->spec().name : std::string();

    // Actions, as their buttons allow them.
    if (run_.running()) {
        if (run_.paused()) {
            if (!run_.stopping() && !run_.stoppingWhenIdle()) add(TR("Resume"), name, [this] { setRunPaused(false); });
        } else if (!run_.stopping()) {
            add(TR("Pause"), name, [this] { setRunPaused(true); });
        }
        if (!run_.stopping()) {
            add(TR("Finish the running games, then stop"), name, [this] { stopRun(false); });
            add(TR("Stop now (running games are lost)"), name, [this] { stopRun(true); });
        }
    } else {
        if (unfinished_) add(TR("Resume"), unfinished_->spec.name, [this] { resumeRun(); });
        if (!queue_.empty()) add(TR("Start now"), queue_.front().label, [this] { startFromQueue(); });
    }
    const bool ready = setupChosen() && !setupBadSprt();
    if (ready && !setupProbing() && !run_.running() && !start_after_reads_)
        add(TR("Start"), TR("New tournament"), [this] { startRun(); });
    if (ready) add(TR("Add to the queue"), TR("New tournament"), [this] { enqueue(); });
    if (t && !games_.empty()) {
        add(TR("Export for CCRL…"), name, [this] {
            page_        = Page::Tournament;
            tab_request_ = Tab::Games;
            openCcrlExport();
        });
    }
    if (t && !isMatch()) {
        const auto copy = [&](const char* how, ui::CopyAs as) {
            add(fmt::format("{}: {}", TR("Copy"), how), TR("Standings"), [this, as] { copyStandings(as); });
        };
        copy(TR("As text"), ui::CopyAs::Text);
        copy(TR("As BBCode, for a forum"), ui::CopyAs::Bbcode);
        if (t->spec().format == tour::Format::Gauntlet) {
            copy(TR("Gauntlet table as text"), ui::CopyAs::GauntletText);
            copy(TR("Gauntlet table as BBCode, for a forum"), ui::CopyAs::GauntletBbcode);
        }
    }
    const auto import       = fmt::format("{} · {}", TR("Engines"), TR("Import…"));
    const auto engines_page = [this](std::function<void()> then) {
        return [this, then = std::move(then)] {
            page_ = Page::Engines;
            then();
        };
    };
    add(TR("An engine list…"), import, engines_page([this] { askEngineList(); }));
    add(TR("Every engine of a folder…"), import,
        engines_page([this] { dialogs_.openFolder(Dialogs::Purpose::ImportEngineFolder); }));
    if (!engineFolder().empty())
        add(TR("Refresh: the new engines of the folder"), import, engines_page([this] { refreshEngineFolder(); }));
    add(TR("A rating list, for the reference Elo…"), import, engines_page([this] { openRatingImport(); }));
    if (const auto unchecked = uncheckedEngines(); !unchecked.empty()) {
        add(trf("Check those not checked ({})", unchecked.size()), TR("Engines"),
            engines_page([this, unchecked] { queueChecks(unchecked); }));
    }

    // Pages, and the tabs of the tournament open and of the ratings.
    const auto page = [this](Page p) {
        return [this, p] {
            if (p == Page::Home) tournaments_dirty_ = true;
            if (p == Page::Ratings && page_ != Page::Ratings) game_db_dirty_ = true;
            page_ = p;
        };
    };
    add(TR("Tournaments"), "", page(Page::Home));
    add(TR("New tournament"), "", page(Page::NewMatch));
    if (t) {
        const auto tab = [&](const char* label, Tab which) {
            add(shown(label), name, [this, which] {
                page_        = Page::Tournament;
                tab_request_ = which;
            });
        };
        tab(TR("Overview###tab_overview"), Tab::Overview);
        if (!isMatch()) tab(TR("Standings###tab_standings"), Tab::Standings);
        if (t->spec().format == tour::Format::Knockout) tab(TR("Bracket###tab_bracket"), Tab::Bracket);
        if (!isMatch()) tab(TR("Schedule###tab_schedule"), Tab::Schedule);
        tab(TR("Broadcast###tab_broadcast"), Tab::Broadcast);
        tab(TR("Live"), Tab::Live);
        tab(TR("Games###tab_games"), Tab::Games);
        tab(TR("Openings###tab_openings"), Tab::Openings);
    }
    add(TR("Engines"), "", page(Page::Engines));
    add(TR("Books"), "", page(Page::Books));
    add(TR("Test suites"), "", page(Page::Suites));
    add(TR("Ratings"), "", page(Page::Ratings));
    const auto ratings_tab = [&](const char* label, int index) {
        add(shown(label), TR("Ratings"), [this, index, go = page(Page::Ratings)] {
            go();
            ratings_tab_request_ = index;
        });
    };
    ratings_tab(TR("Rating list###rt_list"), 0);
    ratings_tab(TR("Engine###rt_engine"), 1);
    ratings_tab(TR("Head to head###rt_h2h"), 2);
    ratings_tab(TR("Progress###rt_progress"), 3);
    add(TR("Log"), "", page(Page::Log));
    add(TR("Settings"), "", page(Page::Settings));

    // The tournaments of the home page: one opens as its Open button does.
    for (const auto& s : tournaments_) {
        add(s.spec.name, fmt::format("{} · {}", TR("Tournament"), folderDate(s.dir)), [this, dir = s.dir] {
            openTournament(dir);
            if (!start_error_.empty()) page_ = Page::Home;  // where the reason is shown
        });
    }
    // The engines, each on its card of the Engines page, and the books.
    for (std::size_t i = 0; i < engines_.size(); ++i) {
        const auto& e = engines_[i];
        add(e.name, e.inactive ? fmt::format("{} · {}", TR("Engines"), TR("Inactive")) : std::string(TR("Engines")),
            [this, i] {
                page_            = Page::Engines;
                selected_engine_ = static_cast<int>(i);
                engine_marked_.clear();
            });
    }
    for (std::size_t i = 0; i < books_.size(); ++i) {
        add(books_[i].name, TR("Books"), [this, i] {
            page_          = Page::Books;
            selected_book_ = static_cast<int>(i);
            book_item_     = -1;
        });
    }
    return out;
}

void App::drawPalette() {
    // F1: the guide at the page shown, also from a text field (none uses it).
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) guide::open(guideSection());

    // Ctrl+K, but not while a text field has the keys, nor over another popup or dialog: opening this one would close
    // it.
    const bool ctrl_k = ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_K);
    if (ctrl_k && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        palette_query_.clear();
        palette_request_ = true;
    }
    if (palette_request_) {
        palette_request_   = false;
        palette_selected_  = 0;
        tournaments_dirty_ = true;  // the list of the home page, read again
        ImGui::OpenPopup("##palette");
    }

    // A column in the middle of the window, near its top.
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.12f),
        ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(std::min(px(640), viewport->WorkSize.x - px(32)), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(12)));
    const bool open = ImGui::BeginPopup("##palette", ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();
    if (!open) return;
    refreshTournaments();

    std::function<void()> chosen;
    bool close = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (ctrl_k && !ImGui::IsWindowAppearing());

    // The search field keeps the keys: Up and Down move in the list (the history callback keeps them from the
    // navigation), Enter runs the line chosen.
    // Not while the mouse is down: it may be clicking a line, which needs the press and the release.
    if (ImGui::IsWindowAppearing() || (!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)))
        ImGui::SetKeyboardFocusHere();
    char buffer[256];
    const auto n = std::min(palette_query_.size(), sizeof(buffer) - 1);
    std::copy_n(palette_query_.data(), n, buffer);
    buffer[n] = 0;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##palette_search", TR("A page, an engine, a tournament, an action…"), buffer,
                                 sizeof(buffer), ImGuiInputTextFlags_CallbackHistory,
                                 [](ImGuiInputTextCallbackData*) { return 0; })) {
        palette_query_    = buffer;
        palette_selected_ = 0;
    }

    const auto entries = paletteEntries();
    std::vector<std::string> labels, wheres;
    labels.reserve(entries.size());
    wheres.reserve(entries.size());
    for (const auto& e : entries) {
        labels.push_back(e.label);
        wheres.push_back(e.where);
    }
    const auto order  = palette::rank(palette_query_, labels, &wheres);
    const int matches = static_cast<int>(order.size());

    bool moved = false;
    if (matches > 0) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) palette_selected_++, moved = true;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) palette_selected_--, moved = true;
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) palette_selected_ += 10, moved = true;
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) palette_selected_ -= 10, moved = true;
    }
    palette_selected_ = std::clamp(palette_selected_, 0, std::max(0, matches - 1));
    if (matches > 0 && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
        chosen = entries[static_cast<std::size_t>(order[static_cast<std::size_t>(palette_selected_)])].run;

    ImGui::Spacing();
    if (matches == 0) {
        ImGui::TextDisabled("%s", TR("Nothing matches."));
    } else {
        // Twelve lines in sight, the others scrolled to.
        const float line = ImGui::GetTextLineHeightWithSpacing();
        ImGui::BeginChild("##palette_list", ImVec2(0, line * static_cast<float>(std::min(matches, 12))),
                          ImGuiChildFlags_None, ImGuiWindowFlags_NoNav);
        auto* dl          = ImGui::GetWindowDrawList();
        const ImU32 text  = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const float gap   = px(16);
        ImGuiListClipper clipper;
        clipper.Begin(matches);
        if (moved) clipper.IncludeItemByIndex(palette_selected_);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& e = entries[static_cast<std::size_t>(order[static_cast<std::size_t>(row)])];
                ImGui::PushID(row);
                if (ImGui::Selectable("##line", row == palette_selected_)) chosen = e.run;
                if (moved && row == palette_selected_) ImGui::SetScrollHereY();
                ImGui::PopID();

                // The name on the left, where it belongs on the right; a long name stops before it.
                const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
                const float pad   = ImGui::GetStyle().FramePadding.x * 0.5f;
                const float where = e.where.empty() ? 0.0f : ImGui::CalcTextSize(e.where.c_str()).x;
                if (where > 0) dl->AddText(ImVec2(max.x - pad - where, min.y), muted, e.where.c_str());
                const ImVec4 clip(min.x, min.y, max.x - pad - where - (where > 0 ? gap : 0.0f), max.y);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + pad, min.y), text, e.label.c_str(),
                            nullptr, 0.0f, &clip);
            }
        }
        ImGui::EndChild();
    }

    if (chosen) close = true;
    if (close) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    // Once the palette is closed: an action may open a popup or a dialog of its own.
    if (chosen) chosen();
}

}  // namespace fcg
