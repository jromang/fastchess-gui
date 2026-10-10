// The "Direct" window: one tab per concurrent game.

#include <cmath>

#include <imgui.h>
#include <implot.h>

#include <fmt/include/fmt/core.h>
#include <engine/uci_engine.hpp>

#include "app.hpp"
#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

// PV in UCI notation to SAN, played from `board`. Stops at the first move that does not parse.
std::string pvToSan(chess::Board board, const std::vector<std::string>& pv, std::size_t max_moves) {
    std::string out;
    for (std::size_t i = 0; i < pv.size() && i < max_moves; ++i) {
        const auto move = chess::uci::uciToMove(board, pv[i]);

        chess::Movelist legal;
        chess::movegen::legalmoves(legal, board);
        if (move == chess::Move::NO_MOVE || std::find(legal.begin(), legal.end(), move) == legal.end()) break;

        if (!out.empty()) out += ' ';
        if (board.sideToMove() == chess::Color::WHITE) {
            out += fmt::format("{}.", board.fullMoveNumber());
        } else if (i == 0) {
            out += fmt::format("{}...", board.fullMoveNumber());
        }
        out += chess::uci::moveToSan(board, move);
        board.makeMove<true>(move);
    }
    if (pv.size() > max_moves) out += " …";
    return out;
}

}  // namespace

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

void App::drawLive() {
    if (!ImGui::Begin(TR("Live###live"))) {
        ImGui::End();
        return;
    }

    auto* obs = run_.observer();
    if (!obs) {
        ImGui::TextDisabled("%s", TR("No match yet. Choose two engines, then click Start."));
        ImGui::End();
        return;
    }

    const int slots = obs->slotCount();
    live_slot_      = std::clamp(live_slot_, 0, slots - 1);

    if (ImGui::BeginTabBar("##slots", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton)) {
        for (int i = 0; i < slots; ++i) {
            const bool active = obs->slotActive(i);
            if (!active) ImGui::PushStyleColor(ImGuiCol_Text, ui::MUTED4);
            const auto label = fmt::format("#{}###slot{}", i + 1, i);
            if (ImGui::BeginTabItem(label.c_str())) {
                if (live_slot_ != i) {
                    live_slot_          = i;
                    live_               = {};
                    live_board_version_ = ~0ULL;
                }
                ImGui::EndTabItem();
            }
            if (!active) ImGui::PopStyleColor();
        }
        ImGui::EndTabBar();
    }

    obs->copySlot(live_slot_, live_);

    if (!live_.has_game) {
        ImGui::TextDisabled("%s", TR("No game in this slot yet."));
        ImGui::End();
        return;
    }

    if (live_.version != live_board_version_) {
        live_board_ = chess::Board();
        live_board_.setFen(live_.start_fen);
        for (const auto& m : live_.moves) live_board_.makeMove<true>(m.move);
        live_board_version_ = live_.version;
    }

    const ImVec2 avail     = ImGui::GetContentRegionAvail();
    const float board_size = std::max(160.0f, std::min(avail.y - 4.0f, avail.x * 0.52f));

    // Evaluation shown by the bar: the side to move's current search, else the last move's score.
    float bar = 0.5f;
    {
        std::optional<fastchess::Score> score;
        chess::Color pov = live_.stm;
        const auto& info = live_.info[live_.stm == chess::Color::WHITE ? 0 : 1];
        if (live_.thinking && !info.empty()) {
            const auto parsed = fastchess::engine::UciEngine::parseInfo(info);
            if (parsed.score) score = *parsed.score;
        }
        if (!score) {
            for (auto it = live_.moves.rbegin(); it != live_.moves.rend(); ++it) {
                if (!it->book && it->score) {
                    score = it->score;
                    pov   = it->mover;
                    break;
                }
            }
        }
        if (score) {
            const int sign = pov == chess::Color::WHITE ? 1 : -1;
            if (score->type == fastchess::ScoreType::MATE) {
                bar = evalBarFraction(0, true, sign * static_cast<int>(score->value >= 0 ? 1 : -1));
            } else if (score->type == fastchess::ScoreType::CP) {
                bar = evalBarFraction(sign * static_cast<int>(score->value), false, 0);
            }
        }
    }

    ImGui::BeginGroup();
    BoardDecor decor;
    decor.flipped    = flip_live_;
    decor.eval_white = bar;
    if (!live_.moves.empty()) decor.last_move = live_.moves.back().move;
    drawBoard(atlas_, live_board_, board_size, decor);
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginChild("##live_side", ImVec2(0, board_size), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

    // Players, top of the board first.
    const auto now     = Clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - live_.think_start).count();

    auto player = [&](chess::Color color) {
        const bool white   = color == chess::Color::WHITE;
        const auto& name   = white ? live_.white : live_.black;
        const bool is_a    = name == obs->engineA();
        const bool to_move = live_.active && live_.stm == color;
        const bool thinks  = to_move && live_.thinking;

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
        ImGui::Text("%s  (%s)", name.c_str(), is_a ? "A" : "B");
        if (to_move) ImGui::PopStyleColor();

        // Clock on the right.
        std::string clock;
        if (live_.clocks) {
            auto ms = white ? live_.white_ms : live_.black_ms;
            if (thinks) ms -= elapsed;
            clock = ui::formatClock(ms);
        } else if (thinks) {
            clock = ui::formatClock(elapsed);
        }
        if (!clock.empty()) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(clock.c_str()).x);
            if (thinks) ImGui::PushStyleColor(ImGuiCol_Text, ui::ACCENT);
            ImGui::TextUnformatted(clock.c_str());
            if (thinks) ImGui::PopStyleColor();
        }

        // Search info: the running search for the side to move, else what its last move reported.
        const auto& line = live_.info[white ? 0 : 1];
        if (thinks && !line.empty()) {
            const auto info  = fastchess::engine::UciEngine::parseInfo(line);
            const auto score = info.score ? std::optional<fastchess::Score>(*info.score) : std::nullopt;
            ImGui::TextColored(
                ui::MUTED4, "d%lld/%lld  %s  %s n  %s n/s", static_cast<long long>(info.depth.value_or(0)),
                static_cast<long long>(info.seldepth.value_or(0)), ui::formatScore(score).c_str(),
                ui::formatCount(info.nodes.value_or(0)).c_str(), ui::formatCount(info.nps.value_or(0)).c_str());
            const auto pv = pvToSan(live_board_, info.pv, 12);
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(pv.empty() ? " " : pv.c_str());
            ImGui::PopTextWrapPos();
        } else {
            const LiveMove* last = nullptr;
            for (auto it = live_.moves.rbegin(); it != live_.moves.rend(); ++it) {
                if (it->mover == color && !it->book) {
                    last = &*it;
                    break;
                }
            }
            if (last) {
                ImGui::TextColored(ui::MUTED4, "d%lld/%lld  %s  %s n  %s n/s  %s", static_cast<long long>(last->depth),
                                   static_cast<long long>(last->seldepth), ui::formatScore(last->score).c_str(),
                                   ui::formatCount(last->nodes).c_str(), ui::formatCount(last->nps).c_str(),
                                   ui::formatClock(last->time_ms).c_str());
            } else {
                ImGui::TextDisabled("—");
            }
            ImGui::TextUnformatted(" ");
        }
    };

    player(flip_live_ ? chess::Color::WHITE : chess::Color::BLACK);
    ImGui::Separator();
    player(flip_live_ ? chess::Color::BLACK : chess::Color::WHITE);

    ImGui::Separator();
    ImGui::Text(TR("Game %zu · round %zu"), live_.game_id, live_.round_id + 1);
    if (!live_.active && !live_.result.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1), "%s  %s", live_.result.c_str(), TR(live_.reason));
    }
    {
        const float w =
            ImGui::CalcTextSize(TR("Flip")).x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - w));
        ImGui::Checkbox(TR("Flip"), &flip_live_);
    }

    // Moves, then the evaluation curve.
    const float plot_h  = std::max(90.0f, ImGui::GetContentRegionAvail().y * 0.30f);
    const float moves_h = ImGui::GetContentRegionAvail().y - plot_h - ImGui::GetStyle().ItemSpacing.y;

    static std::size_t last_count = 0;
    ImGui::BeginChild("##moves", ImVec2(0, moves_h), ImGuiChildFlags_Borders);
    if (ImGui::BeginTable("##movetable", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000.").x);
        ImGui::TableSetupColumn(TR("White"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(TR("Black"), ImGuiTableColumnFlags_WidthStretch);

        chess::Board start;
        start.setFen(live_.start_fen);
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

        if (!live_.moves.empty() && live_.moves.front().mover == chess::Color::BLACK) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d.", number);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("…");
            ImGui::TableNextColumn();
            cell(live_.moves[0]);
            i = 1;
            number++;
        }

        for (; i < live_.moves.size(); i += 2, number++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d.", number);
            ImGui::TableNextColumn();
            cell(live_.moves[i]);
            ImGui::TableNextColumn();
            if (i + 1 < live_.moves.size()) cell(live_.moves[i + 1]);
        }

        ImGui::EndTable();
    }
    // Follow the game; repeated for a few frames because the table grows after the first one.
    static int follow_frames = 0;
    if (live_.moves.size() != last_count) {
        last_count    = live_.moves.size();
        follow_frames = 3;
    }
    if (follow_frames > 0) {
        ImGui::SetScrollY(ImGui::GetScrollMaxY());
        follow_frames--;
    }
    ImGui::EndChild();

    std::vector<float> evals;
    evals.reserve(live_.moves.size());
    float previous = 0.0f;
    for (const auto& m : live_.moves) {
        const float v = m.book ? std::nanf("") : ui::whitePawns(m.score, m.mover);
        if (std::isfinite(v)) previous = v;
        evals.push_back(previous);
    }
    plotEval("##live_eval", evals, -1, std::max(60.0f, ImGui::GetContentRegionAvail().y));

    ImGui::EndChild();
    ImGui::End();
}

}  // namespace fcg
