// The "Direct" window: one tab per concurrent game.

#include <cmath>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>

#include <fmt/include/fmt/format.h>
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

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

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
        drawLiveGame(slot_views_[static_cast<std::size_t>(live_slot_)], "##focus", true, live_slot_);
    } else {
        ImGui::NewLine();
        const float avail = ImGui::GetContentRegionAvail().x;
        const float grid  = std::max(px(360), avail * 0.48f);
        ImGui::BeginChild("##mosaic", ImVec2(grid, 0));
        drawMosaic(grid);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##focus_panel", ImVec2(0, 0));
        drawLiveGame(slot_views_[static_cast<std::size_t>(live_slot_)], "##focus", true, live_slot_);
        ImGui::EndChild();
    }

    ui::endPanel();
}

void App::drawMosaic(float width) {
    auto* obs        = run_.observer();
    const int slots  = obs->slotCount();
    const float gap  = ImGui::GetStyle().ItemSpacing.x;
    const int cols   = std::max(1, static_cast<int>((width + gap) / (px(170) + gap)));
    const float card = (width - gap * (cols - 1) - ImGui::GetStyle().ScrollbarSize) / cols;
    const float line = ImGui::GetTextLineHeightWithSpacing();
    // Board, two name rows, the details and the evaluation bar, inside the card's padding.
    const float info = setup_.tile_info ? line : 0.0f;
    const float h    = (card - 2 * px(6)) + 2 * line + info + px(4) + 2 * ImGui::GetStyle().ItemSpacing.y + 2 * px(5);

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

void App::drawDetached() {
    if (detached_.empty() || !run_.observer()) return;

    for (auto it = detached_.begin(); it != detached_.end();) {
        const int slot = *it;
        refreshSlot(slot);
        bool open = true;
        ImGui::SetNextWindowSize(ImVec2(px(720), px(480)), ImGuiCond_FirstUseEver);
        const auto title = trf("Game #{}", slot + 1) + fmt::format("###detached{}", slot);
        if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoDocking) &&
            slot < static_cast<int>(slot_views_.size())) {
            drawLiveGame(slot_views_[static_cast<std::size_t>(slot)], "##detached", false, slot);
        }
        ImGui::End();
        it = open ? std::next(it) : detached_.erase(it);
    }
}

void App::drawLiveGame(SlotView& view, const char* id, bool allow_detach, int slot) {
    auto* obs      = run_.observer();
    const auto& st = view.state;
    ImGui::PushID(id);

    if (!st.has_game) {
        ImGui::TextDisabled("%s", TR("No game in this slot yet."));
        ImGui::PopID();
        return;
    }

    const ImVec2 avail     = ImGui::GetContentRegionAvail();
    const float info_h     = ImGui::GetTextLineHeightWithSpacing();
    const float board_size = std::max(160.0f, std::min(avail.y - 4.0f - info_h, avail.x * 0.52f));

    ImGui::BeginGroup();
    BoardDecor decor;
    decor.flipped    = view.flip;
    decor.eval_white = evalBar(st);
    if (!st.moves.empty()) decor.last_move = st.moves.back().move;
    drawBoard(atlas_, view.board, board_size, decor);
    drawBoardInfo(atlas_, view.board, board_size, tbPieces());
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginChild("##live_side", ImVec2(0, board_size + info_h), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar);

    // Players, top of the board first.
    const auto now     = Clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - st.think_start).count();
    const bool match   = isMatch();

    auto player = [&](chess::Color color) {
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
        if (match) {
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

        // Search info: the running search for the side to move, else what its last move reported.
        const auto& line = st.info[white ? 0 : 1];
        if (thinks && !line.empty()) {
            const auto info  = fastchess::engine::UciEngine::parseInfo(line);
            const auto score = info.score ? std::optional<fastchess::Score>(*info.score) : std::nullopt;
            ImGui::TextColored(
                ui::MUTED4, "d%lld/%lld  %s  %s n  %s n/s", static_cast<long long>(info.depth.value_or(0)),
                static_cast<long long>(info.seldepth.value_or(0)), ui::formatScore(score).c_str(),
                ui::formatCount(info.nodes.value_or(0)).c_str(), ui::formatCount(info.nps.value_or(0)).c_str());
            const auto pv = pvToSan(view.board, info.pv, 12);
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(pv.empty() ? " " : pv.c_str());
            ImGui::PopTextWrapPos();
        } else {
            const LiveMove* last = nullptr;
            for (auto it = st.moves.rbegin(); it != st.moves.rend(); ++it) {
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

    player(view.flip ? chess::Color::WHITE : chess::Color::BLACK);
    ImGui::Separator();
    player(view.flip ? chess::Color::BLACK : chess::Color::WHITE);

    ImGui::Separator();
    ImGui::Text(TR("Game %zu · round %zu"), st.game_id, st.round_id + 1);
    if (!st.active && !st.result.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1), "%s  %s", st.result.c_str(), TR(st.reason));
    }
    {
        const float flip_w =
            ImGui::CalcTextSize(TR("Flip")).x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
        const float detach_w =
            allow_detach ? ImGui::CalcTextSize(TR("Detach")).x + ImGui::GetStyle().FramePadding.x * 2 + px(8) : 0;
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                      ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - flip_w - detach_w));
        if (allow_detach) {
            if (ImGui::SmallButton(TR("Detach"))) detached_.insert(slot);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", TR("Show this game in its own window, which can be moved to another screen."));
            }
            ImGui::SameLine();
        }
        ImGui::Checkbox(TR("Flip"), &view.flip);
    }

    // Moves, then the evaluation curve.
    const float plot_h  = std::max(90.0f, ImGui::GetContentRegionAvail().y * 0.30f);
    const float moves_h = ImGui::GetContentRegionAvail().y - plot_h - ImGui::GetStyle().ItemSpacing.y;

    ImGui::BeginChild("##moves", ImVec2(0, moves_h), ImGuiChildFlags_Borders);
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
    // Follow the game; repeated for a few frames because the table grows after the first one.
    if (st.moves.size() != view.followed) {
        view.followed      = st.moves.size();
        view.follow_frames = 3;
    }
    if (view.follow_frames > 0) {
        ImGui::SetScrollY(ImGui::GetScrollMaxY());
        view.follow_frames--;
    }
    ImGui::EndChild();

    std::vector<float> evals;
    evals.reserve(st.moves.size());
    float previous = 0.0f;
    for (const auto& m : st.moves) {
        const float v = m.book ? std::nanf("") : ui::whitePawns(m.score, m.mover);
        if (std::isfinite(v)) previous = v;
        evals.push_back(previous);
    }
    plotEval("##live_eval", evals, -1, std::max(60.0f, ImGui::GetContentRegionAvail().y));

    ImGui::EndChild();
    ImGui::PopID();
}

}  // namespace fcg
