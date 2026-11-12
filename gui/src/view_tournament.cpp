// Views of a round robin or a gauntlet: overview (standings, progress, latest results) and the crosstable.

#include <algorithm>
#include <cmath>

#include <imgui.h>
#include <implot.h>

#include "app.hpp"
#include "console.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

constexpr std::size_t CROSSTABLE_MAX  = 200;  // engines; 7 columns more, below ImGui's 512
constexpr std::size_t CROSSTABLE_AUTO = 32;   // a round robin of up to this many opens on its crosstable

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

// The header row, the losses on time explained on their column ("TF", as engine testers write it).
void headers(int columns, int tf_column) {
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    for (int c = 0; c < columns; ++c) {
        ImGui::TableSetColumnIndex(c);
        ImGui::TableHeader(ImGui::TableGetColumnName(c));
        if (c == tf_column && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Losses on time"));
    }
}

void timeoutsCell(int timeouts) {
    if (timeouts == 0) {
        ImGui::TextDisabled("0");
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
    ImGui::Text("%d", timeouts);
    ImGui::PopStyleColor();
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

void App::drawPgnOutError() {
    const auto* t = run_.tournament();
    if (!t || t->spec().pgn_out.empty()) return;
    const auto error = t->pgnOutError();
    if (error.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
    ImGui::TextWrapped(
        "%s", trf("The games wait for {}: {}. Another try after each game.", t->spec().pgn_out, error).c_str());
    ImGui::PopStyleColor();
}

bool App::isMatch() const {
    const auto* t = run_.tournament();
    return !t || t->spec().format == tour::Format::Match;
}

void App::refreshTable() {
    const auto* t = run_.tournament();
    if (!t || isMatch() || table_for_ == games_.size()) return;
    // Hundreds of engines and their games take a while: then the table waits ten times that before the next games
    // count, so that fast games do not hold the window. Asked for (a new anchor, an edit), it is computed now.
    const auto now = std::chrono::steady_clock::now();
    if (table_for_ != ~std::size_t(0) && now < table_next_) return;
    table_      = tour::computeTable(t->spec(), t->results(), anchor_, t->byes());
    shown_      = tour::ordered(table_, t->spec(), standings_order_);
    table_for_  = games_.size();
    table_next_ = now + std::min<std::chrono::steady_clock::duration>(10 * (std::chrono::steady_clock::now() - now),
                                                                      std::chrono::seconds(5));
}

void App::drawStandingsTable(bool compact) {
    // The whole table scrolls under its header in the space left; the compact one is as tall as its rows.
    const auto flags  = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit |
                        ImGuiTableFlags_ScrollX | ImGuiTableFlags_PadOuterX | (compact ? 0 : ImGuiTableFlags_ScrollY);
    const bool swiss  = run_.tournament() && run_.tournament()->spec().format == tour::Format::Swiss;
    const int columns = compact ? 11 : swiss ? 14 : 13;
    if (!ImGui::BeginTable("##standings", columns, flags)) return;
    if (!compact) ImGui::TableSetupScrollFreeze(2, 1);

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
    ImGui::TableSetupColumn("TF");
    if (!compact) {
        if (swiss) ImGui::TableSetupColumn("BH");
        ImGui::TableSetupColumn("SB");
        ImGui::TableSetupColumn(TR("Health"));
    }
    headers(columns, 10);

    // Only the rows in sight: a Swiss or a gauntlet can have hundreds of engines.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>((compact ? table_ : shown_).rows.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            drawStandingsRow(static_cast<std::size_t>(row), compact, swiss);
        }
    }
    ImGui::EndTable();
}

void App::drawStandingsRow(std::size_t i, bool compact, bool swiss) {
    const auto& r = (compact ? table_ : shown_).rows[i];
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%d", static_cast<int>(i + 1));
    ImGui::TableNextColumn();
    if (compact) {
        ImGui::TextUnformatted(r.name.c_str());
    } else if (ImGui::Selectable(fmt::format("{}##st{}", r.name, r.id).c_str(),
                                 standings_engine_ && *standings_engine_ == r.id,
                                 ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
        standings_engine_ = r.id;  // its opponents under the table
    }
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
        ImGui::Text("%+ld", std::lround(r.rating.elo));
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
    ImGui::TableNextColumn();
    timeoutsCell(r.timeouts);
    if (!compact) {
        if (swiss) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%.1f", r.buchholz);
        }
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%.2f", r.sb);
        ImGui::TableNextColumn();
        const auto h = health(r);
        ImGui::PushStyleColor(ImGuiCol_Text, h.empty() ? ui::GOOD : ui::WARN);
        ImGui::TextUnformatted(h.empty() ? "✓" : h.c_str());
        ImGui::PopStyleColor();
    }
}

void App::drawTournamentOverview() {
    refreshTable();
    const auto* t = run_.tournament();
    if (!t) return;

    const float right = std::max(px(320), ImGui::GetContentRegionAvail().x * 0.34f);
    const float left  = ImGui::GetContentRegionAvail().x - right - ImGui::GetStyle().ItemSpacing.x;

    ImGui::BeginChild("##ov_left", ImVec2(left, 0));
    drawPgnOutError();
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

    // The ratings and their intervals, in the height left under the table.
    const float chart_h = ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ItemSpacing.y;
    const auto rated    = std::count_if(table_.rows.begin(), table_.rows.end(), [](const auto& r) { return r.rated; });
    if (rated >= 2 && chart_h >= px(160)) {
        ImGui::Spacing();
        ui::beginCard("elo_card", ImVec2(0, chart_h));
        ImGui::TextUnformatted("Elo");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", TR("95 % interval"));
        std::vector<double> elo, error, rank;
        std::vector<const char*> names;
        for (std::size_t i = 0; i < table_.rows.size(); ++i) {
            const auto& r = table_.rows[i];
            if (!r.rated) continue;
            elo.push_back(r.rating.elo);
            error.push_back(r.rating.error);
            rank.push_back(-static_cast<double>(i));  // the leader on top
            names.push_back(r.name.c_str());
        }
        constexpr auto plot_flags = ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoMouseText;
        if (ImPlot::BeginPlot("##elo_chart", ImVec2(-1, -1), plot_flags)) {
            // Half a row and a tenth of the width spare around the dots and the bars.
            const int n = static_cast<int>(elo.size());
            double low  = 0;
            double high = 0;
            for (int i = 0; i < n; ++i) {
                low  = std::min(low, elo[i] - error[i]);
                high = std::max(high, elo[i] + error[i]);
            }
            const double margin = std::max(10.0, (high - low) * 0.05);
            ImPlot::SetupAxes(nullptr, nullptr);
            ImPlot::SetupAxisLimits(ImAxis_X1, low - margin, high + margin, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, rank.back() - 0.5, 0.5, ImPlotCond_Always);
            ImPlot::SetupAxisTicks(ImAxis_Y1, rank.data(), n, names.data());
            ImPlotSpec bars;
            bars.Flags = ImPlotErrorBarsFlags_Horizontal;
            ImPlot::PlotErrorBars("Elo", elo.data(), rank.data(), error.data(), n, bars);
            ImPlotSpec dots;
            dots.Marker = ImPlotMarker_Circle;
            ImPlot::PlotScatter("Elo", elo.data(), rank.data(), n, dots);
            ImPlot::EndPlot();
        }
        ui::endCard();
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##ov_right", ImVec2(0, 0));

    // Progress by cycle (by round for a Swiss or a knockout).
    ui::beginCard("progress_card");
    const auto& spec = t->spec();
    if (tour::pairedAsItGoes(spec.format)) {
        refreshSchedule();
        const auto round = schedule_.last_round;
        ImGui::TextUnformatted(spec.format == tour::Format::Swiss ? trf("Round {} / {}", round, spec.rounds).c_str()
                                                                  : trf("Round {}", round).c_str());
        const auto all = run_.info().total_games;
        ImGui::ProgressBar(all ? static_cast<float>(games_.size()) / static_cast<float>(all) : 0.0f,
                           ImVec2(-FLT_MIN, px(8)), "");
        ImGui::TextDisabled("%s", trf("{} / {} games", games_.size(), all).c_str());
    } else {
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
                const float f =
                    per ? std::min(1.0f, done[static_cast<std::size_t>(c)] / static_cast<float>(per * 2)) : 0;
                dl->AddRectFilled(ImVec2(x0, pos.y), ImVec2(x0 + w, pos.y + px(10)),
                                  ImGui::GetColorU32(ImGuiCol_FrameBg), px(3));
                if (f > 0) dl->AddRectFilled(ImVec2(x0, pos.y), ImVec2(x0 + w * f, pos.y + px(10)), ui::ACCENT, px(3));
            }
            ImGui::Dummy(ImVec2(width, px(14)));
        }
        ImGui::TextDisabled("%s", trf("{} / {} games", games_.size(), total).c_str());
        if (games_per_hour_ > 0 && total > games_.size()) {
            const double hours = static_cast<double>(total - games_.size()) / games_per_hour_;
            ImGui::TextDisabled(
                "%s",
                trf("{:.0f} games/h · about {} left", games_per_hour_, ui::formatDuration(hours * 3600.0)).c_str());
        }
    }
    ui::endCard();
    drawSubmissionChecklist();  // once it is finished

    // Fair play: the processor time of every engine, measured while it thinks and while its opponents do.
    {
        std::vector<std::string> problems;
        int measured = 0;
        for (std::size_t i = 0; i < totals_.size() && i < spec.participants.size(); ++i) {
            if (!spec.participants[i].active || !cpuUse(totals_[i]).measured) continue;
            measured++;
            const auto problem = cpuProblem(totals_[i], configThreads(spec.participants[i].engine));
            if (!problem.empty()) problems.push_back(fmt::format("{}: {}", spec.participants[i].engine.name, problem));
        }
        if (measured > 0) {
            ImGui::Spacing();
            ui::beginCard("cpu_card");
            ImGui::TextUnformatted(TR("Processor use"));
            ui::help(
                TR("The processor time of each engine's processes, while it thinks (no more cores than its "
                   "threads) and while its opponent does (none without pondering). Measured during the games "
                   "played in the window, and kept with them for when the tournament is resumed."));
            for (const auto& p : problems) {
                ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
                ImGui::TextWrapped("! %s", p.c_str());
                ImGui::PopStyleColor();
            }
            if (problems.empty()) {
                ImGui::TextDisabled("%s", trf("✓ As expected for the {} engines measured", measured).c_str());
            }
            ui::endCard();
        }
    }

    // The latest results, as many as the height left holds; the others under a scroll bar.
    ImGui::Spacing();
    ui::beginCard("latest_card", ImVec2(0, std::max(px(160), ImGui::GetContentRegionAvail().y)));
    ImGui::TextUnformatted(TR("Latest results"));
    const auto* obs = run_.observer();
    ImGui::BeginChild("##latest");
    const float right_x = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("½-½").x;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(games_.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const auto& g = games_[games_.size() - 1 - static_cast<std::size_t>(row)];
            ImGui::TextUnformatted(fmt::format("{} – {}", obs->name(g.white), obs->name(g.black)).c_str());
            ImGui::SameLine(right_x);
            ImGui::PushStyleColor(ImGuiCol_Text, g.white_result > 0   ? ui::GOOD
                                                 : g.white_result < 0 ? ui::BAD
                                                                      : ui::MUTED);
            ImGui::TextUnformatted(g.white_result == 0 ? "½-½" : g.white_result > 0 ? "1-0" : "0-1");
            ImGui::PopStyleColor();
        }
    }
    if (games_.empty()) ImGui::TextDisabled("%s", TR("No game finished yet."));
    ImGui::EndChild();
    ui::endCard();

    ImGui::EndChild();
}

void App::copyStandings(ui::CopyAs as) {
    const auto* t = run_.tournament();
    if (!t || isMatch()) return;
    refreshTable();
    // In the order shown, the line of the event, the games and the book under it.
    const auto& spec  = t->spec();
    const auto footer = tour::footerLine(spec, shown_, t->totalGames());
    std::string text;
    switch (as) {
        case ui::CopyAs::None:
            return;
        case ui::CopyAs::Text:
            text = tour::tableText(shown_, spec.name, footer);
            break;
        case ui::CopyAs::Bbcode:
            text = tour::bbcode(spec.name, tour::forumDetails(spec, shown_), tour::tableText(shown_, "", footer));
            break;
        case ui::CopyAs::GauntletText:
            text = tour::gauntletText(spec, table_, spec.name, t->totalGames());
            break;
        case ui::CopyAs::GauntletBbcode:
            text = tour::bbcode(spec.name, "", tour::gauntletText(spec, table_, "", t->totalGames()));
    }
    ImGui::SetClipboardText(text.c_str());
    Console::instance().add(TR("Standings copied to the clipboard."));
}

void App::drawStandings() {
    refreshTable();
    const auto* t = run_.tournament();
    if (!t) return;

    // The table, or the crosstable for a round robin of a size that fits: a Swiss, a knockout or a gauntlet leaves
    // most of its cells empty, and hundreds of columns are not read (ImGui allows no more than 512).
    const auto format  = t->spec().format;
    const auto engines = t->spec().participants.size();
    const bool fits    = engines <= CROSSTABLE_MAX;
    if (standings_view_ < 0) {
        standings_view_ = format == tour::Format::RoundRobin && engines <= CROSSTABLE_AUTO ? 1 : 0;
        // A gauntlet opens with the opponents of its (first) seed under the table.
        if (format == tour::Format::Gauntlet && engines > 0) standings_engine_ = t->spec().participants.front().id;
    }
    if (!fits) standings_view_ = 0;
    ImGui::RadioButton(TR("Table"), &standings_view_, 0);
    ImGui::SameLine();
    ImGui::BeginDisabled(!fits);
    ImGui::RadioButton(TR("Crosstable"), &standings_view_, 1);
    ImGui::EndDisabled();
    if (!fits && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", trf("Up to {} engines.", CROSSTABLE_MAX).c_str());
    }

    // What the cells show, the rating anchor, export.
    if (standings_view_ == 1) {
        ImGui::SameLine(0, px(30));
        ImGui::TextUnformatted(TR("Cells:"));
        ImGui::SameLine();
        ImGui::RadioButton(TR("Score"), &crosstable_mode_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("+ = −", &crosstable_mode_, 1);
        ImGui::SameLine();
        ImGui::RadioButton(TR("Elo difference"), &crosstable_mode_, 2);
    }

    // The order of the rows (and of the crosstable's columns), which the copies follow.
    ImGui::SameLine(0, px(30));
    ImGui::TextUnformatted(TR("Order:"));
    ImGui::SameLine();
    static constexpr const char* ORDERS[] = {N_("Points"), N_("Score %"), N_("Seed order")};
    ImGui::SetNextItemWidth(px(130));
    if (ImGui::BeginCombo("##order", TR(ORDERS[static_cast<int>(standings_order_)]))) {
        for (int o = 0; o < 3; ++o) {
            if (ImGui::Selectable(TR(ORDERS[o]), static_cast<int>(standings_order_) == o)) {
                standings_order_ = static_cast<tour::Order>(o);
                table_for_       = ~std::size_t(0);
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("Seed order: as the engines were entered, the seeds of a gauntlet first; the "
                                   "columns of a gauntlet's crosstable stay in place."));
    }

    ImGui::SameLine(0, px(30));
    ImGui::TextUnformatted(TR("Anchor:"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(px(180));
    const auto& participants = t->spec().participants;
    const int anchor_index   = anchor_ ? indexOf(t->spec(), anchor_->id) : -1;
    const char* current =
        anchor_index >= 0 ? participants[static_cast<std::size_t>(anchor_index)].engine.name.c_str() : TR("Average");
    if (ImGui::BeginCombo("##anchor", current)) {
        if (ImGui::Selectable(TR("Average"), !anchor_)) {
            anchor_.reset();
            table_for_ = ~std::size_t(0);
        }
        for (std::size_t i = 0; i < participants.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(participants[i].engine.name.c_str(), anchor_index == static_cast<int>(i))) {
                anchor_    = tour::Anchor{participants[i].id, anchor_ ? anchor_->elo : 0.0};
                table_for_ = ~std::size_t(0);
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (anchor_) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(90));
        if (ui::inputDouble("##anchor_elo", &anchor_->elo, "%.0f")) table_for_ = ~std::size_t(0);
    }

    ImGui::SameLine(0, px(30));
    if (const auto as = ui::copyButton("standings", false, format == tour::Format::Gauntlet); as != ui::CopyAs::None)
        copyStandings(as);
    ImGui::SameLine();
    if (ImGui::Button(TR("Export HTML…"))) {
        dialogs_.saveFile(Dialogs::Purpose::ExportStandings, {{"HTML", "html"}},
                          (t->dir() / "standings.html").string());
    }
    refreshTable();

    ImGui::Spacing();
    if (standings_view_ == 1) {
        drawCrosstable();
    } else if (!standings_engine_) {
        drawStandingsTable(false);
    } else {
        const float list_h = std::max(px(160), ImGui::GetContentRegionAvail().y * 0.4f);
        ImGui::BeginChild("##standings_top", ImVec2(0, -list_h));
        drawStandingsTable(false);
        ImGui::EndChild();
        drawOpponents(*standings_engine_);
    }
}

void App::drawOpponents(int id) {
    const auto* t = run_.tournament();
    const auto* p = t ? t->spec().participant(id) : nullptr;
    if (!p) {
        standings_engine_.reset();
        return;
    }

    // The opponents in the order of the table, with the games of each colour: an encounter is a game and the one
    // with colours reversed, and a pair of which one game only counts so far is marked.
    std::vector<std::pair<const tour::Standing*, tour::PairRecord>> rows;
    int unpaired = 0;
    for (const auto& r : shown_.rows) {
        const auto record = shown_.pair(id, r.id);
        if (r.id == id || record.games() == 0) continue;
        rows.emplace_back(&r, record);
        unpaired += record.unpaired;
    }

    ImGui::BeginChild("##opponents", ImVec2(0, 0));
    ImGui::TextUnformatted(trf("{} against each opponent", p->engine.name).c_str());
    ImGui::SameLine();
    if (unpaired > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
        ImGui::TextUnformatted(
            trn("! {} pair with one game only", "! {} pairs with one game only", unpaired, unpaired).c_str());
        ImGui::PopStyleColor();
    } else if (!rows.empty()) {
        ImGui::TextDisabled("%s", TR("every pair with both colours"));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(TR("Close"))) standings_engine_.reset();

    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit |
                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("##opponent_list", 9, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(TR("Opponent"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Games"));
        ImGui::TableSetupColumn(TR("White"));
        ImGui::TableSetupColumn(TR("Black"));
        ImGui::TableSetupColumn("+");
        ImGui::TableSetupColumn("=");
        ImGui::TableSetupColumn("−");
        ImGui::TableSetupColumn(TR("Points"));
        ImGui::TableSetupColumn("%");
        ImGui::TableHeadersRow();
        for (const auto& [r, record] : rows) {
            ImGui::TableNextRow();
            const bool incomplete = record.unpaired > 0;
            if (incomplete) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, (ui::WARN & 0x00FFFFFF) | 0x38000000);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r->name.c_str());
            if (incomplete) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ui::WARN);
                ImGui::TextUnformatted("!");
                ImGui::PopStyleColor();
            }
            if (incomplete && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                ImGui::SetTooltip("%s", trn("{} pair with one game only: the game with colours reversed is missing.",
                                            "{} pairs with one game only: the games with colours reversed are "
                                            "missing.",
                                            record.unpaired, record.unpaired)
                                            .c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d", record.games());
            ImGui::TableNextColumn();
            ImGui::Text("%d", record.white);
            ImGui::TableNextColumn();
            ImGui::Text("%d", record.games() - record.white);
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ui::GOOD);
            ImGui::Text("%d", record.wins);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d", record.draws);
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, ui::BAD);
            ImGui::Text("%d", record.losses);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(half(record.points()).c_str());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%.1f", 100.0 * record.points() / record.games());
        }
        ImGui::EndTable();
    }
    if (rows.empty()) ImGui::TextDisabled("%s", TR("No game finished yet."));
    ImGui::EndChild();
}

void App::drawCrosstable() {
    const auto& rows = shown_.rows;
    if (rows.size() > CROSSTABLE_MAX) return;
    const int n = static_cast<int>(rows.size());

    // The header and the names stay in sight when it scrolls.
    constexpr auto flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX |
                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_PadOuterX;
    const float cell     = crosstable_mode_ == 1 ? px(78) : px(46);
    const float list_h   = crosstable_cell_ ? std::max(px(120), ImGui::GetContentRegionAvail().y * 0.35f) : 0.0f;

    ImGui::BeginChild("##crosstable", ImVec2(0, crosstable_cell_ ? -list_h : 0));
    if (ImGui::BeginTable("##xt", 7 + n, flags)) {
        ImGui::TableSetupScrollFreeze(2, 1);
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn(TR("Engine"));
        for (int c = 0; c < n; ++c) {
            ImGui::TableSetupColumn(fmt::format("{}##c{}", c + 1, c).c_str(), ImGuiTableColumnFlags_WidthFixed, cell);
        }
        ImGui::TableSetupColumn(TR("Points"));
        ImGui::TableSetupColumn(TR("Games"));
        ImGui::TableSetupColumn("TF");
        ImGui::TableSetupColumn("SB");
        ImGui::TableSetupColumn("Elo");
        headers(7 + n, 4 + n);

        // Only the rows in sight; the cells of the columns out of sight are skipped.
        ImGuiListClipper clipper;
        clipper.Begin(n);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& r = rows[static_cast<std::size_t>(i)];
                ImGui::PushID(i);
                ImGui::TableNextRow(0, px(28));
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%d", i + 1);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(r.name.c_str());

                for (int c = 0; c < n; ++c) {
                    if (!ImGui::TableNextColumn()) continue;
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
                    if (ImGui::Selectable(fmt::format("{}##x{}", text, c).c_str(), selected, 0,
                                          ImVec2(cell - px(8), 0)) &&
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
                timeoutsCell(r.timeouts);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%.2f", r.sb);
                ImGui::TableNextColumn();
                if (r.rated) {
                    ImGui::Text("%+ld ±%.0f", std::lround(r.rating.elo), r.rating.error);
                } else {
                    ImGui::TextDisabled("—");
                }
                ImGui::PopID();
            }
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

namespace fcg {

void App::drawBracket() {
    refreshTable();
    const auto* t = run_.tournament();
    if (!t) return;

    const auto brackets = t->brackets();
    const auto& spec    = t->spec();
    const auto name     = [&](int id) { return spec.participant(id) ? spec.participant(id)->engine.name : "?"; };
    if (brackets.empty()) {
        ImGui::TextDisabled("%s", TR("No round yet."));
        return;
    }

    // Who went through a round: the players of the next one, or for the last round, a match decided.
    const auto advanced = [&](uint32_t round, int id) {
        const auto next = brackets.find(round + 1);
        if (next == brackets.end()) return false;
        for (const auto& place : next->second) {
            if (std::find(place.begin(), place.end(), id) != place.end()) return true;
        }
        return false;
    };

    const float column = px(220);
    ImGui::BeginChild("##bracket", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& [round, places] : brackets) {
        if (round > 1) ImGui::SameLine(0, px(24));
        ImGui::BeginGroup();
        const bool final_round = places.size() == 1 && brackets.rbegin()->first == round;
        ImGui::TextDisabled("%s", final_round ? TR("Final") : trf("Round {}", round).c_str());

        for (std::size_t i = 0; i < places.size(); ++i) {
            const auto& place = places[i];
            ImGui::PushID(static_cast<int>(round * 1000 + i));
            ui::beginCard("place", ImVec2(column, 0));
            for (std::size_t k = 0; k < place.size(); ++k) {
                const int id     = place[k];
                const bool bye   = place.size() == 1;
                const double pts = bye ? 0 : table_.pair(id, place[1 - k]).points();
                bool won         = advanced(round, id);
                if (final_round && !bye && t->finished())
                    won = pts > table_.pair(place[1 - k], id).points() ||
                          (pts == table_.pair(place[1 - k], id).points() && k == 0);
                ImGui::PushStyleColor(ImGuiCol_Text, won ? ui::GOOD : ImGui::GetColorU32(ImGuiCol_Text));
                ImGui::TextUnformatted(name(id).c_str());
                ImGui::PopStyleColor();
                ImGui::SameLine(column - px(48));
                if (bye) {
                    ImGui::TextDisabled("%s", TR("bye"));
                } else {
                    ImGui::Text("%s", half(pts).c_str());
                }
            }
            ui::endCard();
            ImGui::PopID();
        }
        ImGui::EndGroup();
    }
    ImGui::EndChild();
}

}  // namespace fcg
