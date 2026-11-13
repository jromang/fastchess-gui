// The live view: the mosaic of the games being played, and the parts of the game followed (board, players, searches,
// moves, chart), beside it or each in a panel of its own; detached boards.

#include <cmath>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include <fmt/include/fmt/format.h>
#include <engine/uci_engine.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "layout.hpp"
#include "ui.hpp"

namespace fcg {

int plotEval(const char* id, const std::vector<float>& evals, int marker_ply, float height) {
    int clicked = -1;

    constexpr ImPlotFlags flags = ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus |
                                  ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText;

    if (ImPlot::BeginPlot(id, ImVec2(-1, height), flags)) {
        const double n = std::max<double>(20, static_cast<double>(evals.size()));
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoGridLines,
                          ImPlotAxisFlags_NoGridLines);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, n, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -8.5, 8.5, ImPlotCond_Always);
        ImPlot::SetupAxisTicks(ImAxis_Y1, -8, 8, 5);

        if (!evals.empty()) {
            std::vector<float> xs(evals.size());
            for (std::size_t i = 0; i < xs.size(); ++i) xs[i] = static_cast<float>(i + 1);

            ImPlotSpec area;
            area.FillColor = ImVec4(0.85f, 0.85f, 0.85f, 0.25f);
            ImPlot::PlotShaded("##area", xs.data(), evals.data(), static_cast<int>(evals.size()), 0.0, area);

            ImPlotSpec line;
            line.LineColor  = ImVec4(0.43f, 0.66f, 0.87f, 1.0f);
            line.LineWeight = 1.5f;
            ImPlot::PlotLine("##eval", xs.data(), evals.data(), static_cast<int>(evals.size()), line);
        }

        const double zero = 0;
        ImPlotSpec zero_spec;
        zero_spec.LineColor = ImVec4(0.5f, 0.5f, 0.5f, 0.6f);
        zero_spec.Flags     = ImPlotInfLinesFlags_Horizontal;
        ImPlot::PlotInfLines("##zero", &zero, 1, zero_spec);

        if (marker_ply > 0) {
            const double x = marker_ply;
            ImPlotSpec marker;
            marker.LineColor = ImVec4(0.95f, 0.75f, 0.3f, 0.8f);
            ImPlot::PlotInfLines("##marker", &x, 1, marker);
        }

        if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const auto mouse = ImPlot::GetPlotMousePos();
            clicked          = static_cast<int>(std::lround(mouse.x));
        }

        ImPlot::EndPlot();
    }

    return clicked;
}

int plotWdl(const char* id, const WdlSeries& wdl, std::size_t plies, int marker_ply, float height) {
    int clicked                 = -1;
    constexpr ImPlotFlags flags = ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus |
                                  ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText;

    if (ImPlot::BeginPlot(id, ImVec2(-1, height), flags)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoGridLines,
                          ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoTickLabels);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, std::max<double>(20, static_cast<double>(plies)), ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, 1, ImPlotCond_Always);

        const int n = static_cast<int>(wdl.ply.size());
        std::vector<float> zero(wdl.ply.size(), 0.0f), one(wdl.ply.size(), 1.0f);
        ImPlotSpec band;
        band.FillColor = ImVec4(0.97f, 0.97f, 0.97f, 1.0f);
        ImPlot::PlotShaded("##win", wdl.ply.data(), zero.data(), wdl.win.data(), n, band);
        band.FillColor = ImVec4(0.74f, 0.74f, 0.74f, 1.0f);
        ImPlot::PlotShaded("##draw", wdl.ply.data(), wdl.win.data(), wdl.not_lost.data(), n, band);
        band.FillColor = ImVec4(0.32f, 0.32f, 0.32f, 1.0f);
        ImPlot::PlotShaded("##loss", wdl.ply.data(), wdl.not_lost.data(), one.data(), n, band);

        if (marker_ply > 0) {
            const double x = marker_ply;
            ImPlotSpec marker;
            marker.LineColor = ImVec4(0.95f, 0.75f, 0.3f, 0.8f);
            ImPlot::PlotInfLines("##marker", &x, 1, marker);
        }
        if (ImPlot::IsPlotHovered()) {
            const auto mouse = ImPlot::GetPlotMousePos();
            const auto it    = std::lower_bound(wdl.ply.begin(), wdl.ply.end(), static_cast<float>(mouse.x));
            if (it != wdl.ply.end()) {
                const auto k = static_cast<std::size_t>(it - wdl.ply.begin());
                ImGui::SetTooltip("%s", trf("White {:.0f} % · draw {:.0f} % · Black {:.0f} %", 100 * wdl.win[k],
                                            100 * (wdl.not_lost[k] - wdl.win[k]), 100 * (1 - wdl.not_lost[k]))
                                            .c_str());
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) clicked = static_cast<int>(std::lround(mouse.x));
        }
        ImPlot::EndPlot();
    }
    return clicked;
}

int pvAgreement(const std::string& previous_pv, const std::string& pv) {
    // The previous PV starts with the opponent's move: what it expected next is from its second move on.
    std::istringstream expected(previous_pv), actual(pv);
    std::string a, b;
    expected >> a;
    int n = 0;
    while (expected >> a && actual >> b && a == b) ++n;
    return n;
}

const char* pgnFieldOption(uint8_t field) {
    // As the checkboxes of the setup's PGN options name them.
    switch (field) {
        case PGN_NODES:
            return TR("nodes");
        case PGN_SELDEPTH:
            return "seldepth";
        case PGN_NPS:
            return "nps";
        case PGN_TBHITS:
            return "tbhits";
        case PGN_PV:
            return "pv";
        case PGN_DECISION:
            return TR("decision time");
        default:
            return "";
    }
}

bool moveChartChoice(const char* id, MoveChart& chart) {
    const char* names[] = {TR("Evaluation"), TR("Time"),    TR("Depth"),       TR("Nodes"),
                           TR("Speed"),      TR("TB hits"), TR("PV agreement")};
    int choice          = static_cast<int>(chart);
    ImGui::SetNextItemWidth(ImGui::CalcTextSize(TR("PV agreement")).x + ImGui::GetFrameHeight() * 2);
    if (!ImGui::Combo(id, &choice, names, IM_ARRAYSIZE(names))) return false;
    chart = static_cast<MoveChart>(choice);
    return true;
}

int plotMoves(const char* id, MoveChart chart, const MoveSeries& series, const std::array<std::string, 2>& names,
              std::size_t plies, int marker_ply, float height, const char* empty) {
    int clicked = -1;
    if (series.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", empty);
        ImGui::PopStyleColor();
        return clicked;
    }

    constexpr ImPlotFlags flags = ImPlotFlags_NoTitle | ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect;
    if (ImPlot::BeginPlot(id, ImVec2(-1, height), flags)) {
        ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_NoGridLines,
                          ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisLimits(ImAxis_X1, 0, std::max<double>(20, static_cast<double>(plies)), ImPlotCond_Always);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);
        if (chart == MoveChart::Nodes || chart == MoveChart::Speed || chart == MoveChart::TbHits) {
            ImPlot::SetupAxisFormat(ImAxis_Y1, [](double v, char* buf, int size, void*) {
                return std::snprintf(buf, static_cast<std::size_t>(size), "%s",
                                     ui::formatCount(static_cast<uint64_t>(std::max(0.0, v))).c_str());
            });
        }

        // White's engine light blue, Black's orange; the extra values thinner, with the same colour.
        const ImVec4 colours[2] = {ImVec4(0.43f, 0.66f, 0.87f, 1.0f), ImVec4(0.93f, 0.6f, 0.3f, 1.0f)};
        for (int side = 0; side < 2; ++side) {
            const auto& xs = series.ply[side];
            const int n    = static_cast<int>(xs.size());
            if (n == 0) continue;
            const auto label = names[side] + "##" + std::to_string(side);
            ImPlotSpec spec;
            spec.LineColor  = colours[side];
            spec.FillColor  = colours[side];
            spec.LineWeight = 1.5f;
            if (chart == MoveChart::Agreement) {
                spec.FillAlpha = 0.8f;
                ImPlot::PlotBars(label.c_str(), xs.data(), series.value[side].data(), n, 0.8, spec);
            } else {
                ImPlot::PlotLine(label.c_str(), xs.data(), series.value[side].data(), n, spec);
            }
            if (std::any_of(series.extra[side].begin(), series.extra[side].end(),
                            [](float v) { return std::isfinite(v); })) {
                ImPlotSpec extra  = spec;
                extra.LineWeight  = 1.0f;
                extra.LineColor.w = 0.55f;
                const auto extra_label =
                    (chart == MoveChart::Time ? trf("{}: decision", names[side]) : trf("{}: seldepth", names[side])) +
                    "##x" + std::to_string(side);
                ImPlot::PlotLine(extra_label.c_str(), xs.data(), series.extra[side].data(), n, extra);
            }
        }

        if (marker_ply > 0) {
            const double x = marker_ply;
            ImPlotSpec marker;
            marker.LineColor = ImVec4(0.95f, 0.75f, 0.3f, 0.8f);
            ImPlot::PlotInfLines("##marker", &x, 1, marker);
        }
        if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            clicked = static_cast<int>(std::lround(ImPlot::GetPlotMousePos().x));
        }
        ImPlot::EndPlot();
    }
    return clicked;
}

void wdlLine(const std::array<int64_t, 3>& wdl) {
    const double sum = std::max<int64_t>(1, wdl[0] + wdl[1] + wdl[2]);
    const auto text =
        trf("W {:.0f} · D {:.0f} · L {:.0f} %", 100 * wdl[0] / sum, 100 * wdl[1] / sum, 100 * wdl[2] / sum);
    ImGui::TextColored(ui::MUTED4, "%s", text.c_str());
    ImGui::SameLine();

    // The three shares in a thin bar, vertically centred on the text.
    const float width = ImGui::GetFontSize() * 6.0f, height = ImGui::GetFontSize() * 0.4f;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float y  = p.y + (ImGui::GetTextLineHeight() - height) / 2;
    const float x1 = p.x + width * static_cast<float>(wdl[0] / sum);
    const float x2 = x1 + width * static_cast<float>(wdl[1] / sum);
    auto* dl       = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(x1, y + height), IM_COL32(110, 190, 120, 255));
    dl->AddRectFilled(ImVec2(x1, y), ImVec2(x2, y + height), IM_COL32(160, 160, 160, 255));
    dl->AddRectFilled(ImVec2(x2, y), ImVec2(p.x + width, y + height), IM_COL32(215, 95, 95, 255));
    ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
}

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

// Width of the panel beside the followed board: players, moves and charts.
float sidePanel() { return px(330); }

// Height of a mosaic card `card` wide: the board, two name rows, the details and the evaluation bar, inside the
// card's padding.
float cardHeight(float card, bool tile_info) {
    const float line = ImGui::GetTextLineHeightWithSpacing();
    return (card - 2 * px(6)) + 2 * line + (tile_info ? line : 0.0f) + px(4) + 2 * ImGui::GetStyle().ItemSpacing.y +
           2 * px(5);
}

struct MosaicGrid {
    int cols     = 1;
    float card   = 0;      // width of a card
    bool scrolls = false;  // too many games for the room: cards of the smallest size, under a scroll bar

    [[nodiscard]] float width() const {
        return cols * card + (cols - 1) * ImGui::GetStyle().ItemSpacing.x +
               (scrolls ? ImGui::GetStyle().ScrollbarSize : 0.0f);
    }
};

// The number of columns that gives `n` games the largest boards in `size`, like the tiles of a video call.
MosaicGrid mosaicGrid(int n, ImVec2 size, bool tile_info) {
    const ImVec2 gap  = ImGui::GetStyle().ItemSpacing;
    const float extra = cardHeight(0, tile_info);
    MosaicGrid grid;
    for (int cols = 1; cols <= n; ++cols) {
        const int rows   = (n + cols - 1) / cols;
        const float wide = (size.x - gap.x * (cols - 1)) / cols;
        const float tall = (size.y - gap.y * (rows - 1)) / rows - extra;
        const float card = std::floor(std::min(wide, tall) - 1.0f);  // a pixel spare, against a stray scroll bar
        if (card > grid.card) grid = {cols, card, false};
    }
    if (grid.card < px(120)) {
        const float width = size.x - ImGui::GetStyle().ScrollbarSize;
        grid.cols         = std::max(1, static_cast<int>((width + gap.x) / (px(120) + gap.x)));
        grid.card         = std::floor((width - gap.x * (grid.cols - 1)) / grid.cols);
        grid.scrolls      = true;
    }
    return grid;
}

// Evaluation shown by the bar: the side to move's current search, else the last move's score.
float evalBar(const SlotState& st) {
    std::optional<fastchess::Score> score;
    chess::Color pov = st.stm;
    const auto& info = st.info[st.stm == chess::Color::WHITE ? 0 : 1];
    if (st.thinking && !info.empty()) {
        const auto parsed = fastchess::engine::UciEngine::parseInfo(info);
        if (parsed.score) score = *parsed.score;
    }
    if (!score) {
        for (auto it = st.moves.rbegin(); it != st.moves.rend(); ++it) {
            if (!it->book && it->score) {
                score = it->score;
                pov   = it->mover;
                break;
            }
        }
    }
    if (!score) return 0.5f;
    const int sign = pov == chess::Color::WHITE ? 1 : -1;
    if (score->type == fastchess::ScoreType::MATE) {
        return evalBarFraction(0, true, sign * static_cast<int>(score->value >= 0 ? 1 : -1));
    }
    if (score->type == fastchess::ScoreType::CP)
        return evalBarFraction(sign * static_cast<int>(score->value), false, 0);
    return 0.5f;
}

// The colours of the engines in the live view: White's light blue, Black's orange.
ImU32 engineColour(chess::Color color, int alpha) {
    return color == chess::Color::WHITE ? IM_COL32(110, 168, 222, alpha) : IM_COL32(237, 153, 77, alpha);
}

// The move the side to move thinks of and the reply it expects (the first two moves of its PV), each in the colour of
// the engine that would play it, the reply lighter; none between searches.
std::vector<BoardArrow> pvArrows(const SlotState& st, const chess::Board& board) {
    std::vector<BoardArrow> arrows;
    const auto& line = st.info[st.stm == chess::Color::WHITE ? 0 : 1];
    if (!st.active || !st.thinking || line.empty()) return arrows;
    const auto moves = legalPrefix(board, fastchess::engine::UciEngine::parseInfo(line).pv, 2);
    // The reply first, so that the move is drawn over it.
    if (moves.size() > 1) arrows.push_back({moves[1], engineColour(~st.stm, 150)});
    if (!moves.empty()) arrows.push_back({moves[0], engineColour(st.stm, 210)});
    return arrows;
}

std::string clockText(const SlotState& st, chess::Color color, int64_t elapsed) {
    const bool thinks = st.active && st.stm == color && st.thinking;
    if (st.clocks) {
        auto ms = color == chess::Color::WHITE ? st.white_ms : st.black_ms;
        if (thinks) ms -= elapsed;
        return ui::formatClock(ms);
    }
    return thinks ? ui::formatClock(elapsed) : std::string();
}

}  // namespace

void App::refreshSlot(int slot) {
    auto* obs = run_.observer();
    if (!obs || slot < 0) return;
    if (static_cast<int>(slot_views_.size()) < obs->slotCount())
        slot_views_.resize(static_cast<std::size_t>(obs->slotCount()));
    if (slot >= static_cast<int>(slot_views_.size())) return;

    auto& v = slot_views_[static_cast<std::size_t>(slot)];
    obs->copySlot(slot, v.state);
    // A gauntlet's new game, from its seed's side; Flip turns it over until the next game.
    if (setup_.live_seed_side && v.state.has_game && v.state.game_id != v.turned_for) {
        v.turned_for = v.state.game_id;
        if (const auto* t = run_.tournament()) {
            if (const auto black = seedHasBlack(t->spec(), v.state.white, v.state.black)) v.flip = *black;
        }
    }
    if (v.state.has_game && v.state.version != v.board_version) {
        v.board = chess::Board();
        v.board.setFen(v.state.start_fen);
        for (const auto& m : v.state.moves) v.board.makeMove<true>(m.move);
        v.board_version = v.state.version;
    }
}

int App::tbPieces() {
    const auto* t = run_.tournament();
    if (t != tb_tournament_) {
        tb_tournament_ = t;
        tb_pieces_     = t ? tablebasePieces(t->spec().conditions.tb_adjudication) : 0;
    }
    return tb_pieces_;
}

void App::drawLive() {
    if (!ui::beginPanel(TR("Live###live"))) {
        ui::endPanel();
        return;
    }

    auto* obs = run_.observer();
    if (!obs) {
        ImGui::TextDisabled("%s", TR("No match yet. Choose two engines, then click Start."));
        ui::endPanel();
        return;
    }

    const int slots = obs->slotCount();
    live_slot_      = std::clamp(live_slot_, 0, slots - 1);
    for (int i = 0; i < slots; ++i) refreshSlot(i);

    // The parts of the followed game that have no panel of their own open are shown here, beside the mosaic.
    const unsigned parts = livePartsHere();

    // Mosaic of every game with the chosen one beside it, or one game with tabs.
    const bool mosaic = live_mosaic_ && slots > 1;
    if (slots > 1) {
        if (ImGui::RadioButton(TR("Mosaic"), live_mosaic_)) live_mosaic_ = true;
        ImGui::SameLine();
        if (ImGui::RadioButton(TR("One game"), !live_mosaic_)) live_mosaic_ = false;
        ImGui::SameLine(0, px(24));
        if (mosaic) {
            if (ImGui::Checkbox(TR("Details under the boards"), &setup_.tile_info)) saveSetupSoon();
            ui::help(TR("The 50-move counter, the material and the pieces left, under every board of the mosaic."));
            ImGui::SameLine(0, px(24));
        }
    }

    if (const auto* t = run_.tournament(); t && t->spec().format == tour::Format::Gauntlet) {
        if (ImGui::Checkbox(TR("Seed at the bottom"), &setup_.live_seed_side)) {
            for (auto& v : slot_views_) v.turned_for = ~std::size_t{0};  // turned at once
            saveSetupSoon();
        }
        ui::help(
            TR("Each new game of the gauntlet is shown from the side of its seed, the engine that meets all the "
               "others. Flip still turns the board over."));
        ImGui::SameLine(0, px(24));
    }

    if (!mosaic) {
        if (ImGui::BeginTabBar("##slots", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton)) {
            for (int i = 0; i < slots; ++i) {
                const bool active = obs->slotActive(i);
                if (!active) ImGui::PushStyleColor(ImGuiCol_Text, ui::MUTED4);
                const auto label = fmt::format("#{}###slot{}", i + 1, i);
                if (ImGui::BeginTabItem(label.c_str())) {
                    live_slot_ = i;
                    ImGui::EndTabItem();
                }
                if (!active) ImGui::PopStyleColor();
            }
            ImGui::EndTabBar();
        }
        if (parts != 0) drawLiveGame(slot_views_[static_cast<std::size_t>(live_slot_)], "##focus", parts, live_slot_);
    } else {
        ImGui::NewLine();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float gap    = ImGui::GetStyle().ItemSpacing.x;
        if (parts == 0) {
            // The followed game in panels of its own: the mosaic takes the whole room.
            const auto grid = mosaicGrid(slots, avail, setup_.tile_info);
            ImGui::BeginChild("##mosaic", ImVec2(0, 0));
            drawMosaic(grid.cols, grid.card);
            ImGui::EndChild();
        } else if (avail.x < px(700)) {
            // Narrow: the mosaic above, the followed game under it.
            const ImVec2 strip(avail.x, std::floor(avail.y * 0.4f));
            const auto grid = mosaicGrid(slots, strip, setup_.tile_info);
            ImGui::BeginChild("##mosaic", ImVec2(0, strip.y));
            drawMosaic(grid.cols, grid.card);
            ImGui::EndChild();
        } else {
            // The followed game gets the width its board needs at full height, beside its panel, within 40 to 70 % of
            // the width; the mosaic the rest, less what its cards leave unused. Without its board, the width of the
            // panel beside it.
            const bool board = parts & LiveBoard;
            const bool side  = parts & ~LiveBoard;
            const float want = (board ? avail.y - ImGui::GetTextLineHeightWithSpacing() : 0.0f) +
                               (board && side ? gap : 0.0f) + (side ? sidePanel() : 0.0f);
            const float focus =
                board ? std::clamp(want, avail.x * 0.4f, avail.x * 0.7f) : std::min(want, avail.x * 0.5f);
            const auto grid   = mosaicGrid(slots, ImVec2(avail.x - focus - gap, avail.y), setup_.tile_info);
            ImGui::BeginChild("##mosaic", ImVec2(std::min(grid.width(), avail.x - focus - gap), 0));
            drawMosaic(grid.cols, grid.card);
            ImGui::EndChild();
            ImGui::SameLine();
        }
        if (parts != 0) {
            ImGui::BeginChild("##focus_panel", ImVec2(0, 0));
            drawLiveGame(slot_views_[static_cast<std::size_t>(live_slot_)], "##focus", parts, live_slot_);
            ImGui::EndChild();
        }
    }

    ui::endPanel();
}

void App::drawMosaic(int cols, float card) {
    auto* obs        = run_.observer();
    const int slots  = obs->slotCount();
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const float h    = cardHeight(card, setup_.tile_info);

    const auto now = Clock::now();
    for (int i = 0; i < slots; ++i) {
        auto& v        = slot_views_[static_cast<std::size_t>(i)];
        const auto& st = v.state;
        if (i % cols != 0) ImGui::SameLine();

        const bool selected = i == live_slot_;
        ImGui::PushStyleColor(ImGuiCol_Border, selected ? ImGui::ColorConvertU32ToFloat4(ui::ACCENT)
                                                        : ImGui::ColorConvertU32ToFloat4(ui::CARD_BD));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::CARD_BG));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(6), px(5)));
        ImGui::BeginChild(fmt::format("##card{}", i).c_str(), ImVec2(card, h),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        const float inner   = ImGui::GetContentRegionAvail().x;
        const auto elapsed  = std::chrono::duration_cast<std::chrono::milliseconds>(now - st.think_start).count();
        const auto name_row = [&](chess::Color color) {
            const auto& name = color == chess::Color::WHITE ? st.white : st.black;
            const auto clock = clockText(st, color, elapsed);
            const bool moves = st.active && st.stm == color;
            ImGui::PushStyleColor(ImGuiCol_Text, moves ? ui::ACCENT : ImGui::GetColorU32(ImGuiCol_Text));
            // A name too long for the card ends with "…" before the clock.
            const char* text = st.has_game ? name.c_str() : "—";
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float end  = pos.x + inner - ImGui::CalcTextSize(clock.c_str()).x - px(6);
            ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), pos, ImVec2(end, pos.y + line), end, text, nullptr,
                                      nullptr);
            ImGui::Dummy(ImVec2(end - pos.x, ImGui::GetTextLineHeight()));
            ImGui::PopStyleColor();
            if (!clock.empty()) {
                ImGui::SameLine(inner - ImGui::CalcTextSize(clock.c_str()).x + ImGui::GetStyle().WindowPadding.x);
                ImGui::TextDisabled("%s", clock.c_str());
            }
        };

        const auto top    = v.flip ? chess::Color::WHITE : chess::Color::BLACK;
        const auto bottom = v.flip ? chess::Color::BLACK : chess::Color::WHITE;
        name_row(top);
        if (st.has_game) {
            BoardDecor decor;
            decor.flipped    = v.flip;
            decor.eval_white = -1.0f;
            if (!st.moves.empty()) decor.last_move = st.moves.back().move;
            drawBoard(atlas_, v.board, inner, decor);
        } else {
            ImGui::Dummy(ImVec2(inner, inner));
        }
        name_row(bottom);
        if (setup_.tile_info) {
            if (st.has_game) {
                drawBoardInfo(atlas_, v.board, inner, tbPieces());
            } else {
                ImGui::Dummy(ImVec2(inner, ImGui::GetTextLineHeight()));
            }
        }

        // A thin evaluation bar and the move number.
        {
            ImDrawList* dl  = ImGui::GetWindowDrawList();
            const auto pos  = ImGui::GetCursorScreenPos();
            const float f   = st.has_game ? evalBar(st) : 0.5f;
            const float bar = px(4);
            dl->AddRectFilled(pos, ImVec2(pos.x + inner, pos.y + bar), IM_COL32(25, 25, 25, 255), bar);
            dl->AddRectFilled(pos, ImVec2(pos.x + inner * f, pos.y + bar), IM_COL32(236, 236, 236, 255), bar);
            dl->AddRect(pos, ImVec2(pos.x + inner, pos.y + bar), IM_COL32(130, 130, 130, 255), bar);  // seen on white
            ImGui::Dummy(ImVec2(inner, bar));
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        if (ImGui::IsItemClicked()) live_slot_ = i;
        if (ImGui::IsItemHovered() && st.has_game) {
            ImGui::SetTooltip("%s – %s\n%s", st.white.c_str(), st.black.c_str(),
                              trf("Game {} · move {}", st.game_id, (st.moves.size() + 1) / 2).c_str());
        }
    }
}

// The board as large as `size`, with the 50-move counter and the material under it.
void App::drawLiveBoard(SlotView& view, float size) {
    const auto& st = view.state;
    ImGui::BeginGroup();
    BoardDecor decor;
    decor.flipped    = view.flip;
    decor.eval_white = evalBar(st);
    if (!st.moves.empty()) decor.last_move = st.moves.back().move;
    if (setup_.live_arrows) decor.arrows = pvArrows(st, view.board);
    drawBoard(atlas_, view.board, size, decor);
    drawBoardInfo(atlas_, view.board, size, tbPieces());
    ImGui::EndGroup();
}

// The board alone, as large as the room allows, in the middle of it.
void App::drawLiveBoardFitted(SlotView& view) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float info_h = ImGui::GetTextLineHeightWithSpacing();
    const float size   = std::floor(std::max(px(120), std::min(avail.x, avail.y - info_h - 4.0f)));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - size) * 0.5f));
    drawLiveBoard(view, size);
}

// A player: the name and the clock, and with `search` what its engine is searching (else what its last move
// reported).
void App::drawLivePlayer(SlotView& view, chess::Color color, bool search) {
    auto* obs          = run_.observer();
    const auto& st     = view.state;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - st.think_start).count();
    const bool white   = color == chess::Color::WHITE;
    const auto& name   = white ? st.white : st.black;
    const bool to_move = st.active && st.stm == color;
    const bool thinks  = to_move && st.thinking;

    // A small disc in the side's colour.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r  = ImGui::GetTextLineHeight() * 0.32f;
    const ImVec2 c(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.5f);
    dl->AddCircleFilled(c, r, white ? IM_COL32(236, 236, 236, 255) : IM_COL32(25, 25, 25, 255), 16);
    dl->AddCircle(c, r, IM_COL32(130, 130, 130, 255), 16);
    ImGui::Dummy(ImVec2(r * 2, ImGui::GetTextLineHeight()));
    ImGui::SameLine();

    if (to_move) ImGui::PushStyleColor(ImGuiCol_Text, ui::ACCENT);
    if (isMatch() && obs) {
        ImGui::Text("%s  (%s)", name.c_str(), name == obs->engineA() ? "A" : "B");
    } else {
        ImGui::TextUnformatted(name.c_str());
    }
    if (to_move) ImGui::PopStyleColor();

    // Clock on the right.
    const auto clock = clockText(st, color, elapsed);
    if (!clock.empty()) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(clock.c_str()).x);
        if (thinks) ImGui::PushStyleColor(ImGuiCol_Text, ui::ACCENT);
        ImGui::TextUnformatted(clock.c_str());
        if (thinks) ImGui::PopStyleColor();
    }
    if (!search) return;

    // Search info: the running search for the side to move, else what its last move reported. Wrapped in a
    // narrow panel.
    ImGui::PushTextWrapPos(0);
    const auto& line = st.info[white ? 0 : 1];
    if (thinks && !line.empty()) {
        const auto info  = fastchess::engine::UciEngine::parseInfo(line);
        const auto score = info.score ? std::optional<fastchess::Score>(*info.score) : std::nullopt;
        ImGui::TextColored(ui::MUTED4, "d%lld/%lld  %s  %s n  %s n/s", static_cast<long long>(info.depth.value_or(0)),
                           static_cast<long long>(info.seldepth.value_or(0)), ui::formatScore(score).c_str(),
                           ui::formatCount(info.nodes.value_or(0)).c_str(),
                           ui::formatCount(info.nps.value_or(0)).c_str());
        if (info.wdl) wdlLine(*info.wdl);
        const auto pv = pvToSan(view.board, info.pv, 12);
        ImGui::TextUnformatted(pv.empty() ? " " : pv.c_str());
    } else {
        const LiveMove* last = nullptr;
        for (auto it = st.moves.rbegin(); it != st.moves.rend(); ++it) {
            if (it->mover == color && !it->book) {
                last = &*it;
                break;
            }
        }
        if (last) {
            const auto decision =
                last->decision_ms ? trf(" (decided at {})", ui::formatClock(*last->decision_ms)) : std::string();
            ImGui::TextColored(ui::MUTED4, "d%lld/%lld  %s  %s n  %s n/s  %s%s", static_cast<long long>(last->depth),
                               static_cast<long long>(last->seldepth), ui::formatScore(last->score).c_str(),
                               ui::formatCount(last->nodes).c_str(), ui::formatCount(last->nps).c_str(),
                               ui::formatClock(last->time_ms).c_str(), decision.c_str());
            if (last->wdl) wdlLine(*last->wdl);
        } else {
            ImGui::TextDisabled("—");
        }
        ImGui::TextUnformatted(" ");
    }
    ImGui::PopTextWrapPos();
}

// The game and round, the result once over, and the board's buttons: Detach (not in a detached board), Arrows, Flip.
void App::drawLiveGameLine(SlotView& view, int slot, bool allow_detach) {
    const auto& st = view.state;
    ImGui::Text(TR("Game %zu · round %zu"), st.game_id, st.round_id + 1);
    if (!st.active && !st.result.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1), "%s  %s", st.result.c_str(), TR(st.reason));
    }
    const auto check_w = [](const char* label) {
        return ImGui::CalcTextSize(label).x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
    };
    const float flip_w   = check_w(TR("Flip"));
    const float arrows_w = check_w(TR("Arrows")) + ImGui::GetStyle().ItemSpacing.x;
    const float detach_w =
        allow_detach ? ImGui::CalcTextSize(TR("Detach")).x + ImGui::GetStyle().FramePadding.x * 2 + px(8) : 0;
    ImGui::SameLine();
    if (ImGui::GetContentRegionAvail().x < flip_w + arrows_w + detach_w) {
        ImGui::NewLine();  // on a line of their own in a narrow panel
    } else {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - flip_w - arrows_w - detach_w);
    }
    if (allow_detach) {
        if (ImGui::SmallButton(TR("Detach"))) showPanel(layout::detachedPanel(slot));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", TR("Show this game in a panel of its own, which can be docked beside the others "
                                       "or moved to another screen."));
        }
        ImGui::SameLine();
    }
    if (ImGui::Checkbox(TR("Arrows"), &setup_.live_arrows)) saveSetupSoon();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", TR("The move the side to move is thinking of, and the reply it expects, in the "
                                   "colours of the engines that would play them."));
    }
    ImGui::SameLine();
    ImGui::Checkbox(TR("Flip"), &view.flip);
}

// The moves played, kept scrolled to the last one while the list is at its end.
void App::drawLiveMoves(SlotView& view, float height) {
    const auto& st = view.state;
    ImGui::BeginChild("##moves", ImVec2(0, height), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##movetable", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000.").x);
        ImGui::TableSetupColumn(TR("White"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Black"), ImGuiTableColumnFlags_WidthStretch);

        chess::Board start;
        start.setFen(st.start_fen);
        int number      = start.fullMoveNumber();
        std::size_t i   = 0;
        const auto cell = [&](const LiveMove& m) {
            if (m.book) {
                ImGui::TextColored(ui::MUTED4, "%s", m.san.c_str());
                return;
            }
            ImGui::TextUnformatted(m.san.c_str());
            ImGui::SameLine();
            ImGui::TextColored(ui::MUTED4, "%s/%lld", ui::formatScore(m.score).c_str(),
                               static_cast<long long>(m.depth));
        };

        if (!st.moves.empty() && st.moves.front().mover == chess::Color::BLACK) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d.", number);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("…");
            ImGui::TableNextColumn();
            cell(st.moves[0]);
            i = 1;
            number++;
        }

        for (; i < st.moves.size(); i += 2, number++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d.", number);
            ImGui::TableNextColumn();
            cell(st.moves[i]);
            ImGui::TableNextColumn();
            if (i + 1 < st.moves.size()) cell(st.moves[i + 1]);
        }

        ImGui::EndTable();
    }
    // Follow the game, if the list was at its end (or the game is a new one): earlier moves being read stay in sight.
    // Repeated for a few frames because the table grows after the first one.
    if (st.moves.size() != view.followed) {
        const bool new_game = view.followed == 0 || st.moves.size() < view.followed;
        const bool at_end   = view.follow_frames > 0 ||
                              ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetTextLineHeightWithSpacing();
        if (new_game || at_end) view.follow_frames = 3;
        view.followed = st.moves.size();
    }
    if (view.follow_frames > 0) {
        ImGui::SetScrollY(ImGui::GetScrollMaxY());
        view.follow_frames--;
    }
    ImGui::EndChild();
}

// The chart chosen under its menu, filling the room left.
void App::drawLiveChart(SlotView& view) {
    const auto& st = view.state;
    std::vector<float> evals;
    evals.reserve(st.moves.size());
    float previous = 0.0f;
    for (const auto& m : st.moves) {
        const float v = m.book ? std::nanf("") : ui::whitePawns(m.score, m.mover);
        if (std::isfinite(v)) previous = v;
        evals.push_back(previous);
    }
    moveChartChoice("##live_chart", live_chart_);
    const float height = std::max(60.0f, ImGui::GetContentRegionAvail().y);
    if (live_chart_ != MoveChart::Eval) {
        // Read from the engines' output, whatever the PGN options: only what they did not report is missing.
        plotMoves("##live_moves", live_chart_, moveSeries(st.moves, live_chart_), {st.white, st.black}, evals.size(),
                  -1, height,
                  live_chart_ == MoveChart::TbHits ? TR("No tablebase hits.") : TR("Nothing reported yet."));
    } else {
        const auto wdl     = wdlSeries(st.moves);
        const float eval_h = wdl.empty() ? height : std::max(60.0f, std::floor(height * 0.62f));
        plotEval("##live_eval", evals, -1, eval_h);
        if (!wdl.empty()) {
            plotWdl("##live_wdl", wdl, evals.size(), -1,
                    std::max(30.0f, height - eval_h - ImGui::GetStyle().ItemSpacing.y - 1.0f));
        }
    }
}

// The parts beside the board, one under the other: the players with their searches, the game, the moves, the chart.
void App::drawLiveSide(SlotView& view, unsigned parts, int slot, bool allow_detach) {
    const bool players = parts & LivePlayers, searches = parts & LiveSearches;
    if (players || searches) {
        drawLivePlayer(view, view.flip ? chess::Color::WHITE : chess::Color::BLACK, searches);
        ImGui::Separator();
        drawLivePlayer(view, view.flip ? chess::Color::BLACK : chess::Color::WHITE, searches);
        ImGui::Separator();
    }
    if (players) drawLiveGameLine(view, slot, allow_detach);

    const bool moves = parts & LiveMoves;
    if (!(parts & LiveChart)) {
        if (moves) drawLiveMoves(view, std::max(ImGui::GetFrameHeight(), ImGui::GetContentRegionAvail().y));
        return;
    }
    if (!moves) {
        drawLiveChart(view);
        return;
    }
    // Moves, then the evaluation curve.
    const float plot_h = std::max(px(110), ImGui::GetContentRegionAvail().y * 0.30f) +
                         ImGui::GetFrameHeightWithSpacing();  // and its menu
    // Too low for both (a small window): the moves only.
    const float left    = ImGui::GetContentRegionAvail().y;
    const bool chart    = left - plot_h >= ImGui::GetFrameHeightWithSpacing() * 4;
    const float moves_h = chart ? left - plot_h - ImGui::GetStyle().ItemSpacing.y : left;
    drawLiveMoves(view, std::max(ImGui::GetFrameHeight(), moves_h));
    if (chart) drawLiveChart(view);
}

void App::drawLiveGame(SlotView& view, const char* id, unsigned parts, int slot, bool allow_detach) {
    const auto& st = view.state;
    ImGui::PushID(id);

    if (!st.has_game) {
        ImGui::TextDisabled("%s", TR("No game in this slot yet."));
        ImGui::PopID();
        return;
    }

    // Without the board, the other parts fill the room; the board alone is as large as the room allows.
    const unsigned side = parts & ~LiveBoard;
    if (!(parts & LiveBoard) || side == 0) {
        if (parts & LiveBoard) {
            drawLiveBoardFitted(view);
        } else {
            ImGui::BeginChild("##live_side", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
            drawLiveSide(view, side, slot, allow_detach);
            ImGui::EndChild();
        }
        ImGui::PopID();
        return;
    }

    // The board as large as the panel allows, with the players, moves and charts beside it; under it when that
    // gives a much larger board (a narrow, tall panel).
    const ImVec2 avail     = ImGui::GetContentRegionAvail();
    const float info_h     = ImGui::GetTextLineHeightWithSpacing();
    const float beside     = std::min(avail.y - 4.0f - info_h, avail.x - sidePanel() - ImGui::GetStyle().ItemSpacing.x);
    const float under      = std::min(avail.x, (avail.y - info_h) * 0.6f);
    const bool stacked     = beside < under * 0.75f;
    const float board_size = std::floor(std::max(px(160), stacked ? under : beside));

    if (stacked) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (avail.x - board_size) * 0.5f));
    drawLiveBoard(view, board_size);

    float side_h = std::max(avail.y, board_size + info_h);
    if (stacked) {
        side_h = std::max(px(260), ImGui::GetContentRegionAvail().y);
    } else {
        ImGui::SameLine();
    }
    ImGui::BeginChild("##live_side", ImVec2(0, side_h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    drawLiveSide(view, side, slot, allow_detach);
    ImGui::EndChild();
    ImGui::PopID();
}

unsigned App::livePartsHere() const {
    unsigned parts = 0;
    for (const auto& [part, panel] : LIVE_PARTS) {
        if (!panels_open_.count(panel)) parts |= part;
    }
    return parts;
}

// A part of the followed game in its own panel.
void App::drawLivePart(unsigned part) {
    if (!run_.observer() || slot_views_.empty()) {
        ImGui::TextDisabled("%s", TR("No game is being played."));
        return;
    }
    live_slot_ = std::clamp(live_slot_, 0, static_cast<int>(slot_views_.size()) - 1);
    auto& view = slot_views_[static_cast<std::size_t>(live_slot_)];
    if (!view.state.has_game) {
        ImGui::TextDisabled("%s", TR("No game in this slot yet."));
        return;
    }
    ImGui::PushID(static_cast<int>(part));
    switch (part) {
        case LiveBoard:
            drawLiveBoardFitted(view);
            break;
        case LivePlayers:
        case LiveSearches:
            drawLiveSide(view, part, live_slot_, true);
            break;
        case LiveMoves:
            drawLiveMoves(view, std::max(ImGui::GetFrameHeight(), ImGui::GetContentRegionAvail().y));
            break;
        case LiveChart:
            drawLiveChart(view);
            break;
        default:
            break;
    }
    ImGui::PopID();
}

// A detached board: every part of its game.
void App::drawDetachedBoard(int slot) {
    auto* obs = run_.observer();
    if (!obs || slot >= obs->slotCount()) {
        ImGui::TextDisabled("%s", TR("No game is being played."));
        return;
    }
    refreshSlot(slot);
    if (slot < static_cast<int>(slot_views_.size()))
        drawLiveGame(slot_views_[static_cast<std::size_t>(slot)], "##detached", LiveAll, slot, false);
}

}  // namespace fcg
