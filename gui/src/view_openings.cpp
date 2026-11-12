// The "Openings" tab: results by starting position, to tell decisive, drawish and unbalanced openings apart.

#include <algorithm>
#include <unordered_map>

#include <imgui.h>

#include <fmt/include/fmt/format.h>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

bool chess960(const tour::TournamentState& t) { return t.spec().conditions.variant == fastchess::VariantType::FRC; }

const char* SHOW[] = {N_("All openings"), N_("A colour won both games of a pair"), N_("Drawn twice"),
                      N_("Won twice by one engine")};

bool shown(const tour::OpeningStats& o, int show) {
    switch (show) {
        case 1:
            return o.white_twice + o.black_twice > 0;
        case 2:
            return o.drawn_twice > 0;
        case 3:
            return o.one_engine_twice > 0;
        default:
            return true;
    }
}

}  // namespace

void App::refreshOpenings() {
    const auto* t = run_.tournament();
    if (!t || by_opening_for_ == games_.size()) return;

    by_opening_          = tour::resultsByOpening(t->results(), t->encounterOpenings(), chess960(*t));
    by_opening_for_      = games_.size();
    opening_order_dirty_ = true;
    opening_games_for_   = -1;
    if (selected_opening_ >= static_cast<int>(by_opening_.size())) selected_opening_ = -1;
}

std::vector<const tour::OpeningStats*> App::shownOpenings() const {
    std::vector<const tour::OpeningStats*> out;
    for (const int i : opening_order_) out.push_back(&by_opening_[i]);
    return out;
}

void App::drawOpenings() {
    if (!ui::beginPanel(TR("Openings###openings"))) {
        ui::endPanel();
        return;
    }
    refreshOpenings();
    const auto name = [](const tour::OpeningStats& o) { return o.eco.empty() ? o.fen : o.eco + " " + o.name; };

    // Toolbar: what to show, how many, export.
    ImGui::SetNextItemWidth(px(280));
    const char* show[IM_ARRAYSIZE(SHOW)];
    for (int i = 0; i < IM_ARRAYSIZE(SHOW); ++i) show[i] = TR(SHOW[i]);
    if (ImGui::Combo("##opening_show", &opening_show_, show, IM_ARRAYSIZE(SHOW))) opening_order_dirty_ = true;
    ImGui::SameLine();
    ImGui::TextDisabled("%s", trf("{} of {} openings", opening_order_.size(), by_opening_.size()).c_str());
    ImGui::SameLine();
    ImGui::BeginDisabled(opening_order_.empty());
    if (ImGui::Button(TR("Export as EPD…"))) {
        dialogs_.saveFile(Dialogs::Purpose::ExportOpenings, {{"EPD", "epd"}},
                          (run_.info().dir / "openings.epd").string());
    }
    ImGui::EndDisabled();
    ui::help(
        TR("The starting positions of the openings shown, in the order shown: a book of them, to keep them "
           "or to remove them from another book."));

    if (by_opening_.empty()) {
        ImGui::TextDisabled("%s", TR("The openings appear here once their games are played."));
        ui::endPanel();
        return;
    }

    const ImVec2 avail   = ImGui::GetContentRegionAvail();
    const bool detail    = selected_opening_ >= 0;
    const float list_w   = detail ? avail.x * 0.62f : avail.x;
    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                           ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Sortable;

    ImGui::BeginChild("##opening_list", ImVec2(list_w, 0));
    if (ImGui::BeginTable("##openings", 8, flags)) {
        // Wide enough for the sample and for the header with its sort arrow.
        const auto width = [](const char* header, const char* sample) {
            return std::max(ImGui::CalcTextSize(sample).x, ImGui::CalcTextSize(header).x + ImGui::GetFontSize());
        };
        ImGui::TableSetupScrollFreeze(0, 1);
        constexpr auto down = ImGuiTableColumnFlags_PreferSortDescending;  // the largest first
        ImGui::TableSetupColumn(TR("Opening"), ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn(TR("Pairs"), ImGuiTableColumnFlags_DefaultSort | down, width(TR("Pairs"), "00000"));
        ImGui::TableSetupColumn(TR("White"), down, width(TR("White"), "100.0 %"));
        ImGui::TableSetupColumn(TR("Draws"), down, width(TR("Draws"), "100.0 %"));
        ImGui::TableSetupColumn(TR("White ×2"), down, width(TR("White ×2"), "00"));
        ImGui::TableSetupColumn(TR("Black ×2"), down, width(TR("Black ×2"), "00"));
        ImGui::TableSetupColumn("2-0", down, width("2-0", "0000"));
        ImGui::TableSetupColumn(TR("Moves"), down, width(TR("Moves"), "000"));

        // Header tooltips: what each column counts.
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        const char* tips[] = {nullptr,
                              TR("Pairs of games played from this position, colours reversed."),
                              TR("White's score over the games."),
                              TR("Share of drawn games."),
                              TR("Pairs where White won both games: the opening favours White."),
                              TR("Pairs where Black won both games: the opening favours Black."),
                              TR("Pairs where the same engine won both games: the opening tells the engines apart."),
                              TR("Average length of the games, in moves.")};
        for (int c = 0; c < 8; ++c) {
            ImGui::TableSetColumnIndex(c);
            ImGui::TableHeader(ImGui::TableGetColumnName(c));
            if (tips[c] && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tips[c]);
        }

        auto* sort = ImGui::TableGetSortSpecs();
        if (sort && sort->SpecsDirty) {
            opening_order_dirty_ = true;
            sort->SpecsDirty     = false;
        }
        if (opening_order_dirty_) {
            opening_order_.clear();
            for (int i = 0; i < static_cast<int>(by_opening_.size()); ++i) {
                if (shown(by_opening_[i], opening_show_)) opening_order_.push_back(i);
            }
            if (sort && sort->SpecsCount > 0) {
                const auto& spec = sort->Specs[0];
                const auto key   = [&](int i) -> double {
                    const auto& o = by_opening_[i];
                    switch (spec.ColumnIndex) {
                        case 2:
                            return o.whiteScore();
                        case 3:
                            return o.drawRate();
                        case 4:
                            return o.white_twice;
                        case 5:
                            return o.black_twice;
                        case 6:
                            return o.one_engine_twice;
                        case 7:
                            return o.games ? static_cast<double>(o.plies) / o.games : 0.0;
                        default:
                            return o.pairs;
                    }
                };
                const bool up = spec.SortDirection == ImGuiSortDirection_Ascending;
                std::stable_sort(opening_order_.begin(), opening_order_.end(),
                                 [&](int a, int b) { return up ? key(a) < key(b) : key(a) > key(b); });
            }
            opening_order_dirty_ = false;
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(opening_order_.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const int i   = opening_order_[row];
                const auto& o = by_opening_[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const auto label = fmt::format("{}##o{}", name(o), i);
                if (ImGui::Selectable(label.c_str(), selected_opening_ == i, ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_opening_ = selected_opening_ == i ? -1 : i;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", o.fen.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%d", o.pairs);
                ImGui::TableNextColumn();
                ImGui::Text("%.1f %%", 100.0 * o.whiteScore());
                ImGui::TableNextColumn();
                ImGui::Text("%.1f %%", 100.0 * o.drawRate());
                const auto count = [](int n) {
                    ImGui::TableNextColumn();
                    if (n > 0) {
                        ImGui::Text("%d", n);
                    } else {
                        ImGui::TextDisabled("·");
                    }
                };
                count(o.white_twice);
                count(o.black_twice);
                count(o.one_engine_twice);
                ImGui::TableNextColumn();
                ImGui::Text("%.0f", o.games ? o.plies / 2.0 / o.games : 0.0);
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    if (!detail) {
        ui::endPanel();
        return;
    }

    // The position, and its games: a click opens one in the Games tab.
    const auto& o = by_opening_[selected_opening_];
    ImGui::SameLine();
    ImGui::BeginChild("##opening_detail");
    ImGui::TextWrapped("%s", name(o).c_str());
    chess::Board board;
    board.set960(chess960(*run_.tournament()));
    board.setFen(o.fen);
    BoardDecor decor;
    decor.eval_white = -1;
    drawBoard(atlas_, board, std::min(ImGui::GetContentRegionAvail().x, px(320)), decor);
    ImGui::TextDisabled("%s", o.fen.c_str());

    ImGui::Spacing();
    ImGui::TextUnformatted(TR("Its games"));
    if (opening_games_for_ != selected_opening_) {
        // Found once per opening chosen rather than every frame over every game.
        std::unordered_map<uint64_t, int> game_index;  // game number -> index into games_
        for (int i = 0; i < static_cast<int>(games_.size()); ++i) game_index.emplace(games_[i].game_id, i);
        opening_games_.clear();
        for (const auto n : o.game_numbers) {
            if (const auto it = game_index.find(n); it != game_index.end()) opening_games_.push_back(it->second);
        }
        opening_games_for_ = selected_opening_;
    }
    for (const int index : opening_games_) {
        if (index >= static_cast<int>(games_.size())) continue;
        const auto& g    = games_[static_cast<std::size_t>(index)];
        const auto n     = g.game_id;
        const auto label = fmt::format("{}  {} – {}  {}##og{}", g.game_id, run_.observer()->name(g.white),
                                       run_.observer()->name(g.black),
                                       g.white_result > 0   ? "1-0"
                                       : g.white_result < 0 ? "0-1"
                                                            : "½-½",
                                       n);
        if (ImGui::Selectable(label.c_str())) {
            selectGame(index);
            tab_request_ = Tab::Games;
        }
    }
    ImGui::EndChild();
    ui::endPanel();
}

}  // namespace fcg
