// Views of a round robin or a gauntlet: overview (standings, progress, latest results) and the crosstable.

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "app.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

std::string half(double points) {
    const auto whole = static_cast<long>(std::floor(points + 1e-9));
    const bool h     = points - whole > 0.25;
    if (whole == 0 && h) return "½";
    return fmt::format("{}{}", whole, h ? "½" : "");
}

std::string health(const tour::Standing& s) {
    std::string out;
    const auto add = [&](int n, const char* one, const char* many) {
        if (n == 0) return;
        if (!out.empty()) out += ", ";
        out += n == 1 ? std::string(one) : fmt::format(fmt::runtime(many), n);
    };
    add(s.timeouts, TR("1 loss on time"), TR("{} losses on time"));
    add(s.crashes, TR("1 crash"), TR("{} crashes"));
    add(s.illegal, TR("1 illegal move"), TR("{} illegal moves"));
    return out;
}

// Index in the participant list of the participant with this id, -1 if none.
int indexOf(const tour::TournamentSpec& spec, int id) {
    for (std::size_t i = 0; i < spec.participants.size(); ++i) {
        if (spec.participants[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

ImU32 scoreColor(double score, float strength) {
    const ImU32 base = score >= 0.5 ? ui::GOOD : ui::BAD;
    const float a    = std::clamp(static_cast<float>(std::abs(score - 0.5) * 2.0) * strength, 0.06f, 0.75f);
    return (base & 0x00FFFFFF) | (static_cast<ImU32>(a * 255) << 24);
}

}  // namespace

bool App::isMatch() const {
    const auto* t = run_.tournament();
    return !t || t->spec().format == tour::Format::Match;
}

void App::refreshTable() {
    const auto* t = run_.tournament();
    if (!t || isMatch() || table_for_ == games_.size()) return;
    table_     = tour::computeTable(t->spec(), t->results(), anchor_);
    table_for_ = games_.size();
}

void App::drawStandingsTable(bool compact) {
    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit |
                           ImGuiTableFlags_ScrollX | ImGuiTableFlags_PadOuterX;
    const int columns    = compact ? 10 : 12;
    if (!ImGui::BeginTable("##standings", columns, flags)) return;

    ImGui::TableSetupColumn("#");
    ImGui::TableSetupColumn(TR("Engine"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(TR("Games"));
    ImGui::TableSetupColumn(TR("Points"));
    ImGui::TableSetupColumn("%");
    ImGui::TableSetupColumn("Elo");
    ImGui::TableSetupColumn("±");
    ImGui::TableSetupColumn("+");
    ImGui::TableSetupColumn("=");
    ImGui::TableSetupColumn("−");
    if (!compact) {
        ImGui::TableSetupColumn("SB");
        ImGui::TableSetupColumn(TR("Health"));
    }
    ImGui::TableHeadersRow();

    for (std::size_t i = 0; i < table_.rows.size(); ++i) {
        const auto& r = table_.rows[i];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%d", static_cast<int>(i + 1));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.name.c_str());
        if (compact) {
            const auto h = health(r);
            if (!h.empty()) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
                ImGui::TextUnformatted("!");
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", h.c_str());
            }
        }
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%d", r.games);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(half(r.points).c_str());
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%.1f", 100.0 * r.score());
        ImGui::TableNextColumn();
        if (r.rated) {
            ImGui::Text("%+.0f", r.rating.elo);
        } else {
            ImGui::TextDisabled("—");
        }
        ImGui::TableNextColumn();
        if (r.rated && r.rating.error > 0) {
            ImGui::TextDisabled("%.0f", r.rating.error);
        } else {
            ImGui::TextDisabled(" ");
        }
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::GOOD);
        ImGui::Text("%d", r.wins);
        ImGui::PopStyleColor();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%d", r.draws);
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
        ImGui::Text("%d", r.losses);
        ImGui::PopStyleColor();
        if (!compact) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%.2f", r.sb);
            ImGui::TableNextColumn();
            const auto h = health(r);
            ImGui::PushStyleColor(ImGuiCol_Text, h.empty() ? ui::GOOD : ui::WARN);
            ImGui::TextUnformatted(h.empty() ? "✓" : h.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndTable();
}

void App::drawTournamentOverview() {
    refreshTable();
    const auto* t = run_.tournament();
    if (!t) return;

    const float right = std::max(px(320), ImGui::GetContentRegionAvail().x * 0.34f);
    const float left  = ImGui::GetContentRegionAvail().x - right - ImGui::GetStyle().ItemSpacing.x;

    ImGui::BeginChild("##ov_left", ImVec2(left, 0));
    ui::beginCard("standings_card");
    ImGui::TextUnformatted(TR("Standings"));
    ImGui::SameLine();
    if (anchor_) {
        const auto name = run_.observer()->name(static_cast<std::size_t>(std::max(0, indexOf(t->spec(), anchor_->id))));
        ImGui::TextDisabled("%s", trf("Elo: {} = {:.0f}, 95 % interval", name, anchor_->elo).c_str());
    } else {
        ImGui::TextDisabled("%s", TR("Elo relative to the average, 95 % interval"));
    }
    drawStandingsTable(true);
    ui::endCard();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##ov_right", ImVec2(0, 0));

    // Progress by cycle.
    ui::beginCard("progress_card");
    const auto& spec   = t->spec();
    const auto total   = run_.info().total_games;
    const int cycles   = std::max(1, spec.cycles);
    const uint64_t per = total / 2 / static_cast<uint64_t>(cycles);  // encounters per cycle
    std::vector<int> done(static_cast<std::size_t>(cycles), 0);
    for (const auto& g : games_) {
        const auto cycle = per ? static_cast<std::size_t>((g.round_id - 1) / per) : 0;
        if (cycle < done.size()) done[cycle]++;
    }
    int current = 0;
    while (current < cycles - 1 && done[static_cast<std::size_t>(current)] >= static_cast<int>(per * 2)) ++current;
    ImGui::TextUnformatted(trf("Cycle {} / {}", current + 1, cycles).c_str());
    {
        ImDrawList* dl    = ImGui::GetWindowDrawList();
        const auto pos    = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        const float gap   = px(4);
        const float w     = (width - gap * (cycles - 1)) / cycles;
        for (int c = 0; c < cycles; ++c) {
            const float x0 = pos.x + c * (w + gap);
            const float f  = per ? std::min(1.0f, done[static_cast<std::size_t>(c)] / static_cast<float>(per * 2)) : 0;
            dl->AddRectFilled(ImVec2(x0, pos.y), ImVec2(x0 + w, pos.y + px(10)), ImGui::GetColorU32(ImGuiCol_FrameBg),
                              px(3));
            if (f > 0) dl->AddRectFilled(ImVec2(x0, pos.y), ImVec2(x0 + w * f, pos.y + px(10)), ui::ACCENT, px(3));
        }
        ImGui::Dummy(ImVec2(width, px(14)));
    }
    ImGui::TextDisabled("%s", trf("{} / {} games", games_.size(), total).c_str());
    if (games_per_hour_ > 0 && total > games_.size()) {
        const double hours = static_cast<double>(total - games_.size()) / games_per_hour_;
        ImGui::TextDisabled(
            "%s", trf("{:.0f} games/h · about {} left", games_per_hour_, ui::formatDuration(hours * 3600.0)).c_str());
    }
    ui::endCard();

    ImGui::Spacing();
    ui::beginCard("latest_card");
    ImGui::TextUnformatted(TR("Latest results"));
    const auto* obs     = run_.observer();
    const float right_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - px(36);
    for (int i = static_cast<int>(games_.size()) - 1, shown = 0; i >= 0 && shown < 12; --i, ++shown) {
        const auto& g = games_[i];
        ImGui::TextUnformatted(fmt::format("{} – {}", obs->name(g.white), obs->name(g.black)).c_str());
        ImGui::SameLine(right_x);
        ImGui::PushStyleColor(ImGuiCol_Text, g.white_result > 0 ? ui::GOOD : g.white_result < 0 ? ui::BAD : ui::MUTED);
        ImGui::TextUnformatted(g.white_result == 0 ? "½-½" : g.white_result > 0 ? "1-0" : "0-1");
        ImGui::PopStyleColor();
    }
    if (games_.empty()) ImGui::TextDisabled("%s", TR("No game finished yet."));
    ui::endCard();

    ImGui::EndChild();
}

void App::drawStandings() {
    refreshTable();
    const auto* t = run_.tournament();
    if (!t) return;

    // What the cells show, the rating anchor, export.
    ImGui::TextUnformatted(TR("Cells:"));
    ImGui::SameLine();
    ImGui::RadioButton(TR("Score"), &crosstable_mode_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("+ = −", &crosstable_mode_, 1);
    ImGui::SameLine();
    ImGui::RadioButton(TR("Elo difference"), &crosstable_mode_, 2);

    ImGui::SameLine(0, px(30));
    ImGui::TextUnformatted(TR("Anchor:"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(180));
    const auto* obs        = run_.observer();
    const auto& names      = obs->names();
    const int anchor_index = anchor_ ? indexOf(t->spec(), anchor_->id) : -1;
    const char* current    = anchor_index >= 0 ? names[static_cast<std::size_t>(anchor_index)].c_str() : TR("Average");
    if (ImGui::BeginCombo("##anchor", current)) {
        if (ImGui::Selectable(TR("Average"), !anchor_)) {
            anchor_.reset();
            table_for_ = ~std::size_t(0);
        }
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (ImGui::Selectable(names[i].c_str(), anchor_index == static_cast<int>(i))) {
                anchor_    = tour::Anchor{t->spec().participants[i].id, anchor_ ? anchor_->elo : 0.0};
                table_for_ = ~std::size_t(0);
            }
        }
        ImGui::EndCombo();
    }
    if (anchor_) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(90));
        if (ImGui::InputDouble("##anchor_elo", &anchor_->elo, 0, 0, "%.0f")) table_for_ = ~std::size_t(0);
    }

    ImGui::SameLine(0, px(30));
    if (ImGui::Button(TR("Copy as text"))) {
        ImGui::SetClipboardText(tour::tableText(table_, t->spec().name).c_str());
        Console::instance().add(TR("Standings copied to the clipboard."));
    }
    ImGui::SameLine();
    if (ImGui::Button(TR("Export HTML…"))) {
        dialogs_.saveFile(Dialogs::Purpose::ExportStandings, {{"HTML", "html"}},
                          (t->dir() / "standings.html").string());
    }
    refreshTable();

    ImGui::Spacing();
    drawCrosstable();
}

void App::drawCrosstable() {
    const auto& rows = table_.rows;
    const int n      = static_cast<int>(rows.size());

    constexpr auto flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX |
                           ImGuiTableFlags_PadOuterX;
    const float cell     = crosstable_mode_ == 1 ? px(78) : px(46);
    const float list_h   = crosstable_cell_ ? std::max(px(120), ImGui::GetContentRegionAvail().y * 0.35f) : 0.0f;

    ImGui::BeginChild("##crosstable", ImVec2(0, crosstable_cell_ ? -list_h : 0));
    if (ImGui::BeginTable("##xt", 6 + n, flags)) {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn(TR("Engine"));
        for (int c = 0; c < n; ++c) {
            ImGui::TableSetupColumn(fmt::format("{}##c{}", c + 1, c).c_str(), ImGuiTableColumnFlags_WidthFixed, cell);
        }
        ImGui::TableSetupColumn(TR("Points"));
        ImGui::TableSetupColumn(TR("Games"));
        ImGui::TableSetupColumn("SB");
        ImGui::TableSetupColumn("Elo");
        ImGui::TableHeadersRow();

        for (int i = 0; i < n; ++i) {
            const auto& r = rows[static_cast<std::size_t>(i)];
            ImGui::PushID(i);
            ImGui::TableNextRow(0, px(28));
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d", i + 1);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.name.c_str());

            for (int c = 0; c < n; ++c) {
                ImGui::TableNextColumn();
                const auto& col = rows[static_cast<std::size_t>(c)];
                if (col.id == r.id) {
                    ImGui::TextDisabled("—");
                    continue;
                }
                const auto p = table_.pair(r.id, col.id);
                std::string text;
                if (p.games() > 0) {
                    switch (crosstable_mode_) {
                        case 1:
                            text = fmt::format("+{} ={} -{}", p.wins, p.draws, p.losses);
                            break;
                        case 2: {
                            const double s = std::clamp(p.points() / p.games(), 0.01, 0.99);
                            text           = fmt::format("{:+.0f}", -400.0 * std::log10(1.0 / s - 1.0));
                            break;
                        }
                        default:
                            text = half(p.points());
                    }
                    const auto min = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(min.x - px(4), min.y - px(3)),
                                                              ImVec2(min.x + cell - px(4), min.y + px(22)),
                                                              scoreColor(p.points() / p.games(), 1.1f), px(4));
                }
                const bool selected =
                    crosstable_cell_ && crosstable_cell_->first == r.id && crosstable_cell_->second == col.id;
                if (ImGui::Selectable(fmt::format("{}##x{}", text, c).c_str(), selected, 0, ImVec2(cell - px(8), 0)) &&
                    p.games() > 0) {
                    crosstable_cell_ = std::make_pair(r.id, col.id);
                }
                if (ImGui::IsItemHovered() && p.games() > 0) {
                    ImGui::SetTooltip("%s – %s\n+%d =%d -%d (%s / %d)", r.name.c_str(), col.name.c_str(), p.wins,
                                      p.draws, p.losses, half(p.points()).c_str(), p.games());
                }
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(half(r.points).c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d", r.games);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%.2f", r.sb);
            ImGui::TableNextColumn();
            if (r.rated) {
                ImGui::Text("%+.0f ±%.0f", r.rating.elo, r.rating.error);
            } else {
                ImGui::TextDisabled("—");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    if (!crosstable_cell_) return;

    // The games of the selected encounter.
    const auto* t   = run_.tournament();
    const auto& ids = t->spec().participants;
    int a = -1, b = -1;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i].id == crosstable_cell_->first) a = static_cast<int>(i);
        if (ids[i].id == crosstable_cell_->second) b = static_cast<int>(i);
    }
    const auto* obs = run_.observer();
    const auto p    = table_.pair(crosstable_cell_->first, crosstable_cell_->second);

    ImGui::BeginChild("##pair_games", ImVec2(0, 0));
    ImGui::TextUnformatted(
        fmt::format("{} – {}", obs->name(static_cast<std::size_t>(a)), obs->name(static_cast<std::size_t>(b))).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", fmt::format("{} – {} · +{} ={} -{}", half(p.points()), half(p.games() - p.points()),
                                          p.wins, p.draws, p.losses)
                                  .c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Close"))) crosstable_cell_.reset();

    if (ImGui::BeginTable("##pair_list", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, px(60));
        ImGui::TableSetupColumn(TR("White"));
        ImGui::TableSetupColumn(TR("Black"));
        ImGui::TableSetupColumn(TR("Result"), ImGuiTableColumnFlags_WidthFixed, px(60));
        ImGui::TableSetupColumn(TR("End"));
        ImGui::TableHeadersRow();
        for (int i = static_cast<int>(games_.size()) - 1; i >= 0; --i) {
            const auto& g = games_[i];
            const bool in = (g.white == a && g.black == b) || (g.white == b && g.black == a);
            if (!in) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(fmt::format("{}##pg{}", g.game_id, i).c_str(), selected_game_ == i,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                selectGame(i);
                tab_request_ = Tab::Games;
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(obs->name(g.white).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(obs->name(g.black).c_str());
            ImGui::TableNextColumn();
            const int r = g.white == a ? g.white_result : -g.white_result;
            ImGui::PushStyleColor(ImGuiCol_Text, r > 0 ? ui::GOOD : r < 0 ? ui::BAD : ui::MUTED);
            ImGui::TextUnformatted(g.white_result == 0 ? "½-½" : g.white_result > 0 ? "1-0" : "0-1");
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(g.reason < reasons_.size() ? TR(reasons_[g.reason]) : "");
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

}  // namespace fcg
