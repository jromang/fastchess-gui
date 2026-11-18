#include "game_graph.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/include/fmt/format.h>

#include "i18n.hpp"
#include "ui.hpp"

namespace fcg {

namespace {

float px(float v) { return v * ImGui::GetFontSize() / 16.0f; }

bool light() { return ui::currentTheme() == ui::Theme::Light; }

// The graph's own colours, in each theme.
ImU32 plotBg() { return light() ? IM_COL32(216, 222, 231, 255) : IM_COL32(35, 40, 49, 255); }
ImU32 whiteSide() { return light() ? IM_COL32(255, 255, 255, 255) : IM_COL32(207, 214, 224, 255); }
ImU32 blackSide() { return light() ? IM_COL32(69, 77, 92, 255) : IM_COL32(14, 17, 21, 255); }
ImU32 gridLine() { return light() ? IM_COL32(205, 210, 218, 255) : IM_COL32(52, 58, 68, 255); }
ImU32 withAlpha(ImU32 col, float alpha) {
    const auto a =
        static_cast<ImU32>(std::clamp(alpha, 0.0f, 1.0f) * static_cast<float>((col >> IM_COL32_A_SHIFT) & 0xFF));
    return (col & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

ImU32 judgementCol(Judgement j) {
    switch (j) {
        case Judgement::Inaccuracy:
            return IM_COL32(212, 169, 21, 255);
        case Judgement::Mistake:
            return IM_COL32(226, 116, 31, 255);
        case Judgement::Blunder:
            return IM_COL32(200, 45, 40, 255);
        default:
            return ui::MUTED;
    }
}

// 1, 2 or 5 times a power of ten, at least `v`.
float niceMax(float v) {
    if (!(v > 0)) return 1.0f;
    const float p = std::pow(10.0f, std::floor(std::log10(v)));
    for (const float m : {1.0f, 2.0f, 5.0f, 10.0f})
        if (m * p >= v * 0.999f) return m * p;
    return 10.0f * p;
}

std::string unitText(GraphChart chart, float v) {
    switch (chart) {
        case GraphChart::Time:
            return v >= 1.0f || v == 0.0f ? fmt::format("{:g} s", v) : fmt::format("{:g} ms", v * 1000.0f);
        case GraphChart::Nodes:
        case GraphChart::Speed:
        case GraphChart::TbHits:
            return ui::formatCount(static_cast<uint64_t>(std::max(0.0f, v)));
        default:
            return fmt::format("{:g}", v);
    }
}

// Pieces other than kings and pawns, and the pieces on each back rank.
struct Material {
    int majors_minors = 0;
    int back_white = 0, back_black = 0;
};
Material material(const chess::Board& board) {
    Material m;
    auto pieces      = board.occ();
    const auto pawns = board.pieces(chess::PieceType::PAWN);
    const auto kings = board.pieces(chess::PieceType::KING);
    m.majors_minors  = (pieces & ~pawns & ~kings).count();
    m.back_white     = (board.us(chess::Color::WHITE) & chess::Bitboard(chess::Rank::RANK_1)).count();
    m.back_black     = (board.us(chess::Color::BLACK) & chess::Bitboard(chess::Rank::RANK_8)).count();
    return m;
}

void polyline(ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 col, float thickness, bool dashed) {
    if (pts.size() < 2) {
        if (pts.size() == 1) dl->AddCircleFilled(pts[0], thickness, col);
        return;
    }
    if (!dashed) {
        dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), col, ImDrawFlags_None, thickness);
        return;
    }
    // Dashes along the line, its length carried from one segment to the next.
    const float on = px(5), off = px(3);
    float phase = 0;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const ImVec2 a = pts[i - 1], b = pts[i];
        const float len = std::hypot(b.x - a.x, b.y - a.y);
        float t         = 0;
        while (t < len) {
            const float cycle = std::fmod(phase + t, on + off);
            const float left  = cycle < on ? on - cycle : on + off - cycle;
            const float step  = std::min(left, len - t);
            if (cycle < on) {
                const float u0 = t / len, u1 = (t + step) / len;
                dl->AddLine(ImVec2(a.x + (b.x - a.x) * u0, a.y + (b.y - a.y) * u0),
                            ImVec2(a.x + (b.x - a.x) * u1, a.y + (b.y - a.y) * u1), col, thickness);
            }
            t += step;
        }
        phase += len;
    }
}

}  // namespace

GamePhases gamePhases(const ReplayGame& game, bool chess960) {
    GamePhases phases;
    chess::Board board;
    board.set960(chess960);
    if (!game.start_fen.empty()) board.setFen(game.start_fen);
    for (int ply = 0;; ++ply) {
        const auto m = material(board);
        if (phases.middlegame < 0 && (m.majors_minors <= 10 || m.back_white < 4 || m.back_black < 4))
            phases.middlegame = ply;
        if (phases.endgame < 0 && m.majors_minors <= 6) {
            phases.endgame = ply;
            if (phases.middlegame < 0) phases.middlegame = ply;
            break;
        }
        if (ply >= static_cast<int>(game.moves.size())) break;
        const auto move = game.moves[static_cast<std::size_t>(ply)].move;
        if (move == chess::Move::NO_MOVE || board.at(move.from()) == chess::Piece::NONE) break;  // not a game's move
        board.makeMove(move);
    }
    return phases;
}

Phase phaseAt(const GamePhases& phases, int ply) {
    if (phases.endgame >= 0 && ply >= phases.endgame) return Phase::Endgame;
    if (phases.middlegame >= 0 && ply >= phases.middlegame) return Phase::Middlegame;
    return Phase::Opening;
}

float graphPawns(const std::optional<fastchess::Score>& score, chess::Color stm) {
    if (!score) return std::nanf("");
    const float sign = stm == chess::Color::WHITE ? 1.0f : -1.0f;
    if (score->type == fastchess::ScoreType::MATE) return score->value > 0 ? sign * MATE_PAWNS : -sign * MATE_PAWNS;
    if (score->type != fastchess::ScoreType::CP) return std::nanf("");
    return sign * static_cast<float>(score->value) / 100.0f;
}

float graphWin(const std::optional<fastchess::Score>& score, const std::optional<std::array<int64_t, 3>>& wdl,
               chess::Color stm) {
    float own = std::nanf("");
    if (wdl) {
        const auto sum = std::max<int64_t>(1, (*wdl)[0] + (*wdl)[1] + (*wdl)[2]);
        own            = static_cast<float>((*wdl)[0] * 2 + (*wdl)[1]) / static_cast<float>(2 * sum);
    } else if (score && (score->type == fastchess::ScoreType::CP || score->type == fastchess::ScoreType::MATE)) {
        own = static_cast<float>(winChance(*score) / 100.0);
    }
    return stm == chess::Color::WHITE ? own : 1.0f - own;
}

GameGraph buildGameGraph(const ReplayGame& game, bool chess960, GraphChart chart,
                         const std::vector<GraphAnalysis>& analyses, const std::vector<MoveReview>& judged) {
    GameGraph g;
    g.plies = static_cast<int>(game.moves.size());
    g.book.reserve(game.moves.size());
    for (const auto& m : game.moves) g.book.push_back(m.book);
    g.phases = gamePhases(game, chess960);
    {
        chess::Board start;
        start.set960(chess960);
        if (!game.start_fen.empty()) start.setFen(game.start_fen);
        g.first_move  = start.fullMoveNumber();
        g.black_first = start.sideToMove() == chess::Color::BLACK;
    }
    for (int side = 0; side < 2; ++side) {
        const auto name = game.header(side == 0 ? "White" : "Black");
        g.players[static_cast<std::size_t>(side)].name =
            name.empty() || name == "?" ? std::string(side == 0 ? TR("White") : TR("Black")) : name;
    }
    g.players[0].key    = "white";
    g.players[1].key    = "black";
    g.players[0].colour = light() ? IM_COL32(47, 111, 222, 255) : IM_COL32(91, 157, 255, 255);
    g.players[1].colour = light() ? IM_COL32(217, 130, 43, 255) : IM_COL32(240, 163, 91, 255);

    // What the moves' comments hold, for the tabs; the time strip.
    auto& av                                       = g.available;
    av[static_cast<std::size_t>(GraphChart::Eval)] = av[static_cast<std::size_t>(GraphChart::Win)] = true;
    bool white_eval = false, black_eval = false;
    for (std::size_t i = 0; i < game.moves.size(); ++i) {
        const auto& m    = game.moves[i];
        const int side   = m.mover == chess::Color::WHITE ? 0 : 1;
        const auto sidez = static_cast<std::size_t>(side);
        if (m.book) continue;
        if (m.time_ms >= 0) {
            g.time_x[sidez].push_back(static_cast<float>(i + 1));
            g.time_s[sidez].push_back(static_cast<float>(m.time_ms) / 1000.0f);
            av[static_cast<std::size_t>(GraphChart::Time)] = true;
        }
        if (m.depth > 0) av[static_cast<std::size_t>(GraphChart::Depth)] = true;
        if (m.nodes > 0) av[static_cast<std::size_t>(GraphChart::Nodes)] = true;
        if (m.nps > 0) av[static_cast<std::size_t>(GraphChart::Speed)] = true;
        if (m.tbhits > 0) av[static_cast<std::size_t>(GraphChart::TbHits)] = true;
        if (!m.pv.empty() && i > 0 && !game.moves[i - 1].pv.empty())
            av[static_cast<std::size_t>(GraphChart::Predicted)] = true;
        if (m.score) (side == 0 ? white_eval : black_eval) = true;
    }
    if ((white_eval && black_eval) || analyses.size() >= 2) av[static_cast<std::size_t>(GraphChart::Agreement)] = true;
    g.wdl                                         = wdlSeries(game.moves);
    av[static_cast<std::size_t>(GraphChart::Wdl)] = !g.wdl.empty();

    // The side to move in each position.
    const auto stmAt = [&](std::size_t ply) -> chess::Color {
        if (ply < game.moves.size()) return game.moves[ply].mover;
        if (!game.moves.empty()) return ~game.moves.back().mover;
        return g.black_first ? chess::Color::BLACK : chess::Color::WHITE;
    };
    // An analysis's curve: White's pawns and expected score at each position it searched.
    const auto analysisCurve = [&](const GraphAnalysis& a) {
        GraphCurve c;
        c.key    = "analysis:" + a.name;
        c.name   = a.name;
        c.note   = a.note;
        c.colour = a.colour;
        if (!a.review) return c;
        for (std::size_t p = 0; p < a.review->positions.size() && p <= game.moves.size(); ++p) {
            const auto& pos = a.review->positions[p];
            const float v   = graphPawns(pos.score, stmAt(p));
            if (!std::isfinite(v)) continue;
            c.x.push_back(static_cast<float>(p));
            c.y.push_back(v);
            c.win.push_back(graphWin(pos.score, pos.wdl, stmAt(p)));
        }
        return c;
    };

    switch (chart) {
        case GraphChart::Eval:
        case GraphChart::Win:
            for (std::size_t i = 0; i < game.moves.size(); ++i) {
                const auto& m = game.moves[i];
                if (m.book || !m.score) continue;
                auto& c       = g.players[m.mover == chess::Color::WHITE ? 0 : 1];
                const float v = graphPawns(m.score, m.mover);
                if (!std::isfinite(v)) continue;
                c.x.push_back(static_cast<float>(i + 1));
                c.y.push_back(v);
                c.win.push_back(graphWin(m.score, m.wdl, m.mover));
            }
            for (const auto& a : analyses) g.analyses.push_back(analysisCurve(a));
            for (std::size_t i = 0; i < judged.size() && i < game.moves.size(); ++i) {
                if (judged[i].judgement != Judgement::None)
                    g.marks.push_back({static_cast<int>(i + 1), judged[i].judgement, false});
            }
            break;
        case GraphChart::Agreement: {
            // The two players apart: each move's evaluation against the opponent's just before it.
            auto& c  = g.players[0];
            c.key    = "apart";
            c.name   = TR("The players apart");
            c.colour = ui::WARN;
            for (std::size_t i = 1; i < game.moves.size(); ++i) {
                const auto &m = game.moves[i], &prev = game.moves[i - 1];
                if (m.book || prev.book || !m.score || !prev.score) continue;
                const float a = graphPawns(m.score, m.mover), b = graphPawns(prev.score, prev.mover);
                if (!std::isfinite(a) || !std::isfinite(b)) continue;
                c.x.push_back(static_cast<float>(i + 1));
                c.y.push_back(std::min(std::fabs(std::clamp(a, -10.0f, 10.0f) - std::clamp(b, -10.0f, 10.0f)), 10.0f));
            }
            g.players[1] = {};
            // The analyses apart: the largest gap between two of them at each position.
            if (analyses.size() >= 2) {
                std::vector<GraphCurve> curves;
                for (const auto& a : analyses) curves.push_back(analysisCurve(a));
                for (int p = 0; p <= g.plies; ++p) {
                    float lo = 1e9f, hi = -1e9f;
                    int n = 0;
                    for (const auto& c2 : curves) {
                        const auto it = std::lower_bound(c2.x.begin(), c2.x.end(), static_cast<float>(p));
                        if (it == c2.x.end() || *it != static_cast<float>(p)) continue;
                        const float v = std::clamp(c2.y[static_cast<std::size_t>(it - c2.x.begin())], -10.0f, 10.0f);
                        lo = std::min(lo, v), hi = std::max(hi, v), ++n;
                    }
                    if (n < 2) continue;
                    g.bar_x.push_back(static_cast<float>(p));
                    g.bar_y.push_back(hi - lo);
                }
            }
            break;
        }
        default:
            for (std::size_t i = 0; i < game.moves.size(); ++i) {
                const auto& m = game.moves[i];
                if (m.book) continue;
                float value = std::nanf(""), extra = std::nanf("");
                switch (chart) {
                    case GraphChart::Time:
                        if (m.time_ms >= 0) value = static_cast<float>(m.time_ms) / 1000.0f;
                        if (m.decision_ms) extra = static_cast<float>(*m.decision_ms) / 1000.0f;
                        break;
                    case GraphChart::Depth:
                        if (m.depth > 0) value = static_cast<float>(m.depth);
                        if (m.seldepth > 0) extra = static_cast<float>(m.seldepth);
                        break;
                    case GraphChart::Nodes:
                        if (m.nodes > 0) value = static_cast<float>(m.nodes);
                        break;
                    case GraphChart::Speed:
                        if (m.nps > 0) value = static_cast<float>(m.nps);
                        break;
                    case GraphChart::TbHits:
                        value = static_cast<float>(m.tbhits);
                        break;
                    case GraphChart::Predicted:
                        if (i > 0 && !game.moves[i - 1].book && !game.moves[i - 1].pv.empty() && !m.pv.empty())
                            value = static_cast<float>(pvAgreement(game.moves[i - 1].pv, m.pv));
                        break;
                    default:
                        break;
                }
                if (!std::isfinite(value)) continue;
                auto& c = g.players[m.mover == chess::Color::WHITE ? 0 : 1];
                c.x.push_back(static_cast<float>(i + 1));
                c.y.push_back(value);
                c.extra.push_back(extra);
            }
            break;
    }
    return g;
}

std::string graphMoveTip(const ReplayGame& game, int ply, const char* mark) {
    if (ply <= 0 || ply > static_cast<int>(game.moves.size())) return {};
    chess::Board start;
    if (!game.start_fen.empty()) start.setFen(game.start_fen);
    const bool black_first = start.sideToMove() == chess::Color::BLACK;
    const auto& m          = game.moves[static_cast<std::size_t>(ply - 1)];
    const int number       = start.fullMoveNumber() + (ply - 1 + (black_first ? 1 : 0)) / 2;
    std::string tip        = fmt::format("{}{}{}{}", number, m.mover == chess::Color::WHITE ? "." : "…", m.san, mark);
    if (m.book) return tip + "  " + TR("book");
    if (m.score) {
        auto name = game.header(m.mover == chess::Color::WHITE ? "White" : "Black");
        if (name.empty() || name == "?") name = m.mover == chess::Color::WHITE ? TR("White") : TR("Black");
        auto white = *m.score;  // White's point of view
        if (m.mover == chess::Color::BLACK) white.value = -white.value;
        tip += "\n" + trf("{}: {} · depth {}", name, ui::formatScore(white), m.depth);
        if (m.time_ms >= 0) tip += " · " + ui::formatClock(m.time_ms);
    }
    return tip;
}

const char* graphChartName(GraphChart chart) {
    switch (chart) {
        case GraphChart::Eval:
            return TR("Evaluation");
        case GraphChart::Win:
            return TR("Win chances");
        case GraphChart::Wdl:
            return TR("WDL");
        case GraphChart::Time:
            return TR("Time");
        case GraphChart::Depth:
            return TR("Depth");
        case GraphChart::Nodes:
            return TR("Nodes");
        case GraphChart::Speed:
            return TR("Speed");
        case GraphChart::TbHits:
            return TR("TB hits");
        case GraphChart::Agreement:
            return TR("Agreement");
        case GraphChart::Predicted:
            return TR("PV agreement");
        default:
            return "";
    }
}

float foldedGraphHeight() { return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y; }

GraphEvents drawGameGraph(const char* id, const GameGraph& g, GraphView& view, const GraphDraw& d, ImVec2 size) {
    GraphEvents ev;
    ImGui::PushID(id);
    const float fs    = ImGui::GetFontSize();
    const ImVec2 top  = ImGui::GetCursorScreenPos();
    const float width = size.x > 0 ? size.x : ImGui::GetContentRegionAvail().x;
    const auto hidden = [&](const GraphCurve& c) {
        return std::find(view.hidden.begin(), view.hidden.end(), c.key) != view.hidden.end();
    };

    // The tabs, the state of the analysis, the fold.
    const auto missable = [&](GraphChart c) {
        return d.missing && c != GraphChart::Agreement && c != GraphChart::Wdl && c != GraphChart::Eval &&
               c != GraphChart::Win;
    };
    const bool chart_missing = !g.available[static_cast<std::size_t>(view.chart)];
    if (chart_missing && !missable(view.chart)) view.chart = GraphChart::Eval;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(8), ImGui::GetStyle().FramePadding.y));
    // The tabs that fit; the others in a menu, the one shown always among the tabs.
    std::vector<int> tabs, more;
    {
        const float room = width - ImGui::GetFrameHeight() - px(8) -
                           (d.progress.empty() ? 0.0f : ImGui::CalcTextSize(d.progress.c_str()).x + px(10)) -
                           (d.action ? ImGui::CalcTextSize(d.action).x + px(24) : 0.0f) - ImGui::GetFrameHeight() -
                           px(8);
        float used       = 0;
        const auto tab_w = [&](int c) {
            return ImGui::CalcTextSize(graphChartName(static_cast<GraphChart>(c))).x + px(16) + px(2);
        };
        const float current_w = tab_w(static_cast<int>(view.chart));
        for (int c = 0; c < static_cast<int>(GraphChart::Count); ++c) {
            const bool current = c == static_cast<int>(view.chart);
            if (!g.available[static_cast<std::size_t>(c)] && !current) continue;
            const float w          = tab_w(c);
            const bool shown_later = std::find(tabs.begin(), tabs.end(), static_cast<int>(view.chart)) == tabs.end() &&
                                     static_cast<int>(view.chart) > c;
            if (current || used + w + (shown_later ? current_w : 0.0f) <= room) {
                tabs.push_back(c);
                used += w;
            } else {
                more.push_back(c);
            }
        }
    }
    for (const int c : tabs) {
        const auto chart = static_cast<GraphChart>(c);
        const bool on    = view.chart == chart;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? ui::SELECT : IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ui::MUTED4);
        if (c != tabs.front()) ImGui::SameLine(0, px(2));
        if (ImGui::Button(fmt::format("{}##chart{}", graphChartName(chart), c).c_str()) && !on) {
            view.chart      = chart;
            ev.view_changed = true;
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) {
            const char* tip = nullptr;
            switch (chart) {
                case GraphChart::Win:
                    tip =
                        TR("White's winning chances: from the engine's WDL when it gives it, from its score "
                           "otherwise (lichess's formula). Engines' scores compare better here.");
                    break;
                case GraphChart::Wdl:
                    tip =
                        TR("The win, draw and loss shares the engines reported (UCI_ShowWDL), White's wins at the "
                           "bottom.");
                    break;
                case GraphChart::Time:
                    tip = TR("The time of each move, with the decision time when the PGN has it.");
                    break;
                case GraphChart::Depth:
                    tip = TR("The depth of each move, with the seldepth.");
                    break;
                case GraphChart::Agreement:
                    tip =
                        TR("How far apart the evaluations are, in pawns: the two players' (each move against the "
                           "one before) and the analyses' at each position.");
                    break;
                case GraphChart::Predicted:
                    tip = TR("How many plies of an engine's PV repeat what the other engine's PV predicted.");
                    break;
                default:
                    break;
            }
            if (tip) ImGui::SetTooltip("%s", tip);
        }
    }
    std::vector<int> missing;
    if (d.missing) {
        for (int c = 0; c < static_cast<int>(GraphChart::Count); ++c) {
            const auto chart = static_cast<GraphChart>(c);
            if (!g.available[static_cast<std::size_t>(c)] && missable(chart) && chart != view.chart)
                missing.push_back(c);
        }
    }
    if (!more.empty() || !missing.empty()) {
        ImGui::SameLine(0, px(2));
        if (ui::iconOnlyButton("##more_charts", ui::Icon::More, TR("More charts")))
            ImGui::OpenPopup("##more_charts_menu");
        if (ImGui::BeginPopup("##more_charts_menu")) {
            for (const int c : more) {
                if (ImGui::MenuItem(graphChartName(static_cast<GraphChart>(c)))) {
                    view.chart      = static_cast<GraphChart>(c);
                    ev.view_changed = true;
                }
            }
            if (!more.empty() && !missing.empty()) ImGui::Separator();
            for (const int c : missing) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (ImGui::MenuItem(graphChartName(static_cast<GraphChart>(c)))) {
                    view.chart      = static_cast<GraphChart>(c);
                    ev.view_changed = true;
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    if (const auto why = d.missing(static_cast<GraphChart>(c)); !why.empty())
                        ImGui::SetTooltip("%s", why.c_str());
                }
            }
            ImGui::EndPopup();
        }
    }
    ImGui::PopStyleVar();
    {
        const float fold_w = ImGui::GetFrameHeight();
        const float prog_w = d.progress.empty() ? 0.0f : ImGui::CalcTextSize(d.progress.c_str()).x + px(10);
        const float act_w =
            d.action ? ImGui::CalcTextSize(d.action).x + ImGui::GetStyle().FramePadding.x * 2 + px(8) : 0.0f;
        ImGui::SameLine(
            std::max(ImGui::GetCursorPosX(), top.x - ImGui::GetWindowPos().x + width - fold_w - prog_w - act_w));
        if (!d.progress.empty()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", d.progress.c_str());
            ImGui::SameLine();
        }
        if (d.action) {
            if (ImGui::SmallButton(d.action)) ev.action = true;
            if (d.action_tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.action_tip);
            ImGui::SameLine();
        }
        if (ImGui::ArrowButton("##fold", view.folded ? ImGuiDir_Down : ImGuiDir_Up)) {
            view.folded     = !view.folded;
            ev.view_changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", view.folded ? TR("Show the graph") : TR("Fold the graph"));
    }
    if (view.folded) {
        ImGui::PopID();
        return ev;
    }
    // A chart the game has no values for: why, and where to change it.
    if (!g.available[static_cast<std::size_t>(view.chart)]) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", d.missing ? d.missing(view.chart).c_str() : "");
        ImGui::PopStyleColor();
        if (d.pgn_options && ImGui::SmallButton(TR("PGN options"))) ev.pgn_options = true;
        if (d.pgn_options && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", TR("The PGN options of the setup, for the tournaments to come."));
        ImGui::PopID();
        return ev;
    }

    auto* dl         = ImGui::GetWindowDrawList();
    auto* font       = ImGui::GetFont();
    const float tiny = fs * 0.72f;
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);

    // The legend: one item per curve, a click shows or hides it.
    const bool eval_like = view.chart == GraphChart::Eval || view.chart == GraphChart::Win;
    {
        const auto item = [&](const std::string& key, const GraphCurve& c, int kind, bool menu, int index) {
            if (c.name.empty()) return;
            const bool off   = hidden(c);
            const ImVec2 nsz = ImGui::CalcTextSize(c.name.c_str());
            const float note = c.note.empty() ? 0.0f : font->CalcTextSizeA(tiny, FLT_MAX, 0, c.note.c_str()).x + px(5);
            const ImVec2 sz(px(7) + px(14) + px(5) + nsz.x + note + px(9), ImGui::GetTextLineHeight() + px(4));
            ImGui::SameLine(0, px(4));
            if (ImGui::GetCursorScreenPos().x + sz.x > top.x + width) return;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton(key.c_str(), sz)) {
                if (off)
                    view.hidden.erase(std::remove(view.hidden.begin(), view.hidden.end(), c.key), view.hidden.end());
                else
                    view.hidden.push_back(c.key);
                ev.view_changed = true;
            }
            const bool hovered = ImGui::IsItemHovered();
            if (hovered) ImGui::SetTooltip("%s", off ? TR("Click: show it.") : TR("Click: hide it."));
            if (menu && ImGui::BeginPopupContextItem((key + "_menu").c_str())) {
                if (ImGui::MenuItem(TR("Judge the moves with it"), nullptr, index == 0, index != 0)) ev.judge = index;
                if (ImGui::MenuItem(TR("Remove this analysis"))) ev.remove = index;
                ImGui::EndPopup();
            }
            const float alpha = off ? 0.45f : 1.0f;
            dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), hovered ? ui::ACCENT : ui::CARD_BD, sz.y / 2);
            const ImVec2 sw(p.x + px(7), p.y + sz.y / 2);
            if (kind == 1) {
                dl->AddRectFilled(ImVec2(sw.x, sw.y - px(5)), ImVec2(sw.x + px(12), sw.y),
                                  withAlpha(whiteSide(), alpha));
                dl->AddRectFilled(ImVec2(sw.x, sw.y), ImVec2(sw.x + px(12), sw.y + px(5)),
                                  withAlpha(blackSide(), alpha));
                dl->AddRect(ImVec2(sw.x, sw.y - px(5)), ImVec2(sw.x + px(12), sw.y + px(5)),
                            withAlpha(ui::MUTED, alpha));
            } else {
                polyline(dl, {ImVec2(sw.x, sw.y), ImVec2(sw.x + px(14), sw.y)}, withAlpha(c.colour, alpha), px(2.5f),
                         c.dashed);
            }
            const ImVec2 tp(sw.x + px(19), p.y + px(2));
            dl->AddText(tp, withAlpha(text, alpha), c.name.c_str());
            if (!c.note.empty())
                dl->AddText(font, tiny, ImVec2(tp.x + nsz.x + px(5), tp.y + (nsz.y - tiny) * 0.7f),
                            withAlpha(ui::MUTED, alpha), c.note.c_str());
            if (off)
                dl->AddLine(ImVec2(tp.x, p.y + sz.y / 2), ImVec2(tp.x + nsz.x, p.y + sz.y / 2), withAlpha(text, 0.6f));
        };
        ImGui::Dummy(ImVec2(0, 0));
        for (int side = 0; side < 2 && view.chart != GraphChart::Wdl; ++side) {
            const auto& c = g.players[static_cast<std::size_t>(side)];
            if (!c.x.empty()) item(fmt::format("##player{}", side), c, 0, false, -1);
        }
        if (eval_like) {
            for (std::size_t i = 0; i < g.analyses.size(); ++i)
                item(fmt::format("##analysis{}", i), g.analyses[i], i == 0 ? 1 : 0, d.analyses_menu,
                     static_cast<int>(i));
            if (d.can_add) {
                ImGui::SameLine(0, px(4));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::ACCENT));
                if (ImGui::SmallButton(TR("+ Engine"))) ev.add_analysis = true;
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", TR("Another engine analyses the game too."));
            }
        }
        if (view.chart == GraphChart::Agreement && !g.bar_x.empty()) {
            ImGui::SameLine(0, px(10));
            ImGui::TextDisabled("%s", TR("bars: the analyses apart"));
        }
    }

    // The plot: the chart, the phases under it, the time strip, the analysis's band.
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float bottom  = top.y + size.y;
    const float total_h = std::max(px(80), bottom - origin.y - ImGui::GetStyle().ItemSpacing.y);
    const float left = origin.x + px(30), right = top.x + width - px(4);
    const float axis_h  = fs * 0.95f;
    const float band_h  = g.level.empty() ? 0.0f : px(10);
    const bool strip    = !g.time_x[0].empty() || !g.time_x[1].empty();
    const float strip_h = strip ? std::max(px(34), total_h * 0.24f) : 0.0f;
    const float main_t = origin.y, main_b = origin.y + total_h - axis_h - strip_h - band_h - (strip ? px(4) : 0.0f);
    const float axis_y  = main_b + axis_h * 0.5f;
    const float strip_t = main_b + axis_h + px(2), strip_b = strip_t + strip_h;
    const int n  = std::max(20, g.plies);
    const auto X = [&](float ply) { return left + (right - left) * ply / static_cast<float>(n); };

    ImGui::SetCursorScreenPos(ImVec2(left, main_t));
    ImGui::InvisibleButton(
        "##plot", ImVec2(std::max(1.0f, right - left), std::max(1.0f, (strip ? strip_b : main_b + axis_h) - main_t)));
    const bool hovered  = ImGui::IsItemHovered();
    const int hover_ply = std::clamp(
        static_cast<int>(std::lround((ImGui::GetMousePos().x - left) / (right - left) * static_cast<float>(n))), 0,
        g.plies);
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) && hover_ply != d.current)
        ev.go_to = hover_ply;
    if (hovered && d.tip) {
        if (const auto tip = d.tip(hover_ply); !tip.empty()) ImGui::SetTooltip("%s", tip.c_str());
    }

    dl->PushClipRect(ImVec2(origin.x, main_t - fs * 0.5f), ImVec2(right + 1, bottom), true);
    dl->AddRectFilled(ImVec2(left, main_t), ImVec2(right, main_b), plotBg());

    // The book, hatched; the tablebase zone.
    const auto zone_label = [&](float x0, float x1, float y, const char* label, ImU32 col) {
        const float w = font->CalcTextSizeA(tiny, FLT_MAX, 0, label).x;
        if (x1 - x0 > w + px(6)) dl->AddText(font, tiny, ImVec2((x0 + x1 - w) / 2, y - tiny - px(2)), col, label);
    };
    for (int p = 1; p <= g.plies;) {
        if (!g.book[static_cast<std::size_t>(p - 1)]) {
            ++p;
            continue;
        }
        int q = p;
        while (q < g.plies && g.book[static_cast<std::size_t>(q)]) ++q;
        const float x0 = X(static_cast<float>(p - 1)), x1 = X(static_cast<float>(q));
        dl->AddRectFilled(ImVec2(x0, main_t), ImVec2(x1, main_b), withAlpha(ui::MUTED, 0.10f));
        dl->PushClipRect(ImVec2(x0, main_t), ImVec2(x1, main_b), true);
        for (float s = x0 - (main_b - main_t); s < x1; s += px(6))
            dl->AddLine(ImVec2(s, main_b), ImVec2(s + (main_b - main_t), main_t), withAlpha(ui::MUTED, 0.22f));
        dl->PopClipRect();
        zone_label(x0, x1, main_b, TR("book"), ui::MUTED);
        p = q + 1;
    }
    if (g.tablebase >= 0 && g.tablebase <= g.plies) {
        const float x0 = X(static_cast<float>(g.tablebase)), x1 = X(static_cast<float>(g.plies));
        dl->AddRectFilled(ImVec2(x0, main_t), ImVec2(std::max(x1, x0 + px(2)), main_b), withAlpha(ui::GOOD, 0.14f));
        zone_label(x0, x1, main_b, TR("tablebase"), ui::GOOD);
    }

    // The chart's scale.
    const auto tick = [&](float y, const std::string& label, bool strong) {
        dl->AddLine(ImVec2(left, y), ImVec2(right, y), strong ? withAlpha(ui::MUTED, 0.6f) : gridLine());
        const float w = font->CalcTextSizeA(tiny, FLT_MAX, 0, label.c_str()).x;
        dl->AddText(font, tiny, ImVec2(left - px(4) - w, y - tiny / 2), ui::MUTED, label.c_str());
    };
    const auto points = [&](const GraphCurve& c, const std::vector<float>& ys, const auto& Y) {
        std::vector<std::vector<ImVec2>> runs(1);
        for (std::size_t i = 0; i < c.x.size() && i < ys.size(); ++i) {
            if (!std::isfinite(ys[i])) {
                if (!runs.back().empty()) runs.emplace_back();
                continue;
            }
            runs.back().emplace_back(X(c.x[i]), Y(ys[i]));
        }
        return runs;
    };
    const auto draw_curve = [&](const GraphCurve& c, const std::vector<float>& ys, const auto& Y, float thickness) {
        for (const auto& run : points(c, ys, Y)) polyline(dl, run, c.colour, thickness, c.dashed);
    };
    // White's and Black's shares between a curve and its zero line, without seams between the strips.
    const auto draw_area = [&](const GraphCurve& c, const std::vector<float>& ys, const auto& Y, float zero,
                               float alpha) {
        const auto flags = dl->Flags;
        dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        const ImU32 up = withAlpha(whiteSide(), alpha), down = withAlpha(blackSide(), alpha);
        for (const auto& run : points(c, ys, Y)) {
            for (std::size_t i = 1; i < run.size(); ++i) {
                ImVec2 a = run[i - 1], b = run[i];
                const auto quad = [&](ImVec2 p, ImVec2 q) {
                    const ImVec2 pts[4] = {ImVec2(p.x, zero), p, q, ImVec2(q.x, zero)};
                    dl->AddConvexPolyFilled(pts, 4, (p.y + q.y) / 2 <= zero ? up : down);
                };
                if ((a.y < zero && b.y > zero) || (a.y > zero && b.y < zero)) {
                    const float t = (zero - a.y) / (b.y - a.y);
                    const ImVec2 m(a.x + (b.x - a.x) * t, zero);
                    quad(a, m);
                    quad(m, b);
                } else {
                    quad(a, b);
                }
            }
        }
        dl->Flags = flags;
    };
    const auto value_at = [](const GraphCurve& c, const std::vector<float>& ys, int ply) {
        const auto it = std::lower_bound(c.x.begin(), c.x.end(), static_cast<float>(ply));
        if (it == c.x.end() || *it != static_cast<float>(ply)) return std::nanf("");
        return ys[static_cast<std::size_t>(it - c.x.begin())];
    };

    const GraphCurve* judge = !g.analyses.empty() && !hidden(g.analyses[0]) ? &g.analyses[0] : nullptr;
    if (view.chart == GraphChart::Eval) {
        const auto clampY = [](float v) { return std::clamp(v, -EVAL_SCALE, EVAL_SCALE); };
        const auto Y      = [&](float v) {
            return main_t + (main_b - main_t) * (EVAL_SCALE - clampY(v)) / (2 * EVAL_SCALE);
        };
        const float zero = Y(0);
        tick(Y(EVAL_SCALE), "+6", false);
        tick(Y(EVAL_SCALE / 2), "+3", false);
        tick(Y(-EVAL_SCALE / 2), "−3", false);
        tick(Y(-EVAL_SCALE), "−6", false);
        // The area: the judging analysis, or the players' evaluations one after the other.
        if (judge) {
            draw_area(*judge, judge->y, Y, zero, 1.0f);
            const GraphCurve outline{{}, judge->name, {}, withAlpha(text, 0.45f), false, judge->x, judge->y, {}, {}};
            draw_curve(outline, judge->y, Y, 1.0f);
        } else if (g.analyses.empty()) {
            // Without an analysis, the two engines' last evaluations averaged: their common view, without the zigzag
            // of one against the other.
            GraphCurve both;
            std::array<float, 2> last{std::nanf(""), std::nanf("")};
            for (int p = 1; p <= g.plies; ++p) {
                bool changed = false;
                for (std::size_t side = 0; side < 2; ++side) {
                    const auto& c = g.players[side];
                    if (const float v = value_at(c, c.y, p); std::isfinite(v)) {
                        last[side] = std::clamp(v, -EVAL_SCALE * 2, EVAL_SCALE * 2);
                        changed    = true;
                    }
                }
                if (!changed) continue;
                const float v = std::isfinite(last[0]) && std::isfinite(last[1]) ? (last[0] + last[1]) / 2
                                : std::isfinite(last[0])                         ? last[0]
                                                                                 : last[1];
                both.x.push_back(static_cast<float>(p));
                both.y.push_back(v);
            }
            draw_area(both, both.y, Y, zero, 0.7f);
            const GraphCurve outline{{}, {}, {}, withAlpha(text, 0.3f), false, both.x, both.y, {}, {}};
            draw_curve(outline, both.y, Y, 1.0f);
        }
        tick(zero, "0", true);
        for (std::size_t i = 1; i < g.analyses.size(); ++i)
            if (!hidden(g.analyses[i])) draw_curve(g.analyses[i], g.analyses[i].y, Y, px(1.6f));
        for (const auto& c : g.players)
            if (!hidden(c)) draw_curve(c, c.y, Y, px(1.6f));
        // Beyond the scale: the judging curve's edge in the warning colour.
        if (judge) {
            for (std::size_t i = 1; i < judge->x.size(); ++i) {
                const float a = judge->y[i - 1], b = judge->y[i];
                if ((a >= EVAL_SCALE && b >= EVAL_SCALE) || (a <= -EVAL_SCALE && b <= -EVAL_SCALE))
                    dl->AddLine(ImVec2(X(judge->x[i - 1]), Y(a)), ImVec2(X(judge->x[i]), Y(b)), ui::WARN, px(2.5f));
            }
        }
        // The judgements, on the judging curve.
        if (judge) {
            for (const auto& m : g.marks) {
                const float v = value_at(*judge, judge->y, m.ply);
                if (!std::isfinite(v)) continue;
                const ImVec2 c(X(static_cast<float>(m.ply)), Y(v));
                const ImU32 col = judgementCol(m.judgement);
                if (m.hollow) {
                    dl->AddCircleFilled(c, px(4.5f), ui::CARD_BG);
                    dl->AddCircle(c, px(4.5f), col, 0, px(2));
                } else {
                    dl->AddCircleFilled(c, px(4.5f), col);
                    dl->AddCircle(c, px(4.5f), ui::CARD_BG, 0, px(1.5f));
                }
                dl->AddText(font, fs * 0.8f, ImVec2(c.x + px(6), c.y - px(15)), col, judgementMark(m.judgement));
            }
        }
    } else if (view.chart == GraphChart::Win) {
        const auto Y = [&](float w) { return main_b - (main_b - main_t) * std::clamp(w, 0.0f, 1.0f); };
        tick(Y(1), "100", false);
        tick(Y(0), "0", false);
        if (judge) {
            const auto flags = dl->Flags;
            dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
            for (const auto& run : points(*judge, judge->win, Y)) {
                for (std::size_t i = 1; i < run.size(); ++i) {
                    const ImVec2 pts[4] = {ImVec2(run[i - 1].x, main_b), run[i - 1], run[i], ImVec2(run[i].x, main_b)};
                    dl->AddConvexPolyFilled(pts, 4, whiteSide());
                }
            }
            dl->Flags = flags;
            const GraphCurve outline{{}, judge->name, {}, withAlpha(text, 0.45f), false, judge->x, judge->y, {}, {}};
            draw_curve(outline, judge->win, Y, 1.0f);
        }
        tick(Y(0.5f), "50", true);
        for (std::size_t i = 1; i < g.analyses.size(); ++i)
            if (!hidden(g.analyses[i])) draw_curve(g.analyses[i], g.analyses[i].win, Y, px(1.6f));
        for (const auto& c : g.players)
            if (!hidden(c)) draw_curve(c, c.win, Y, px(1.6f));
        if (judge) {
            for (const auto& m : g.marks) {
                const float v = value_at(*judge, judge->win, m.ply);
                if (!std::isfinite(v)) continue;
                const ImVec2 c(X(static_cast<float>(m.ply)), Y(v));
                dl->AddCircleFilled(c, px(4.5f), m.hollow ? ui::CARD_BG : judgementCol(m.judgement));
                dl->AddCircle(c, px(4.5f), m.hollow ? judgementCol(m.judgement) : ui::CARD_BG, 0, px(1.5f));
            }
        }
    } else if (view.chart == GraphChart::Wdl) {
        const auto Y     = [&](float w) { return main_b - (main_b - main_t) * std::clamp(w, 0.0f, 1.0f); };
        const auto flags = dl->Flags;
        dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        const ImU32 draw_col = light() ? IM_COL32(178, 184, 194, 255) : IM_COL32(88, 95, 107, 255);
        const auto& w        = g.wdl;
        for (std::size_t i = 1; i < w.ply.size(); ++i) {
            const float x0 = X(w.ply[i - 1]), x1 = X(w.ply[i]);
            const auto band = [&](float a0, float a1, float b0, float b1, ImU32 col) {
                const ImVec2 pts[4] = {ImVec2(x0, Y(a0)), ImVec2(x0, Y(b0)), ImVec2(x1, Y(b1)), ImVec2(x1, Y(a1))};
                dl->AddConvexPolyFilled(pts, 4, col);
            };
            band(0, 0, w.win[i - 1], w.win[i], whiteSide());
            band(w.win[i - 1], w.win[i], w.not_lost[i - 1], w.not_lost[i], draw_col);
            band(w.not_lost[i - 1], w.not_lost[i], 1, 1, blackSide());
        }
        dl->Flags = flags;
        tick(Y(0.5f), "50", true);
    } else {
        // Values from zero up: the players' lines (bars for the PV agreement), the analyses apart as bars.
        float hi = 0;
        for (const auto& c : g.players) {
            for (const float v : c.y) hi = std::max(hi, v);
            for (const float v : c.extra)
                if (std::isfinite(v)) hi = std::max(hi, v);
        }
        for (const float v : g.bar_y) hi = std::max(hi, v);
        const float top_v = niceMax(view.chart == GraphChart::Agreement ? std::max(hi, 1.0f) : hi);
        const auto Y      = [&](float v) { return main_b - (main_b - main_t) * std::clamp(v / top_v, 0.0f, 1.0f); };
        tick(Y(top_v), unitText(view.chart, top_v), false);
        tick(Y(top_v / 2), unitText(view.chart, top_v / 2), false);
        tick(main_b, "0", true);
        const float bw = std::max(1.0f, (right - left) / static_cast<float>(n) - 1.0f);
        for (std::size_t i = 0; i < g.bar_x.size(); ++i) {
            const float x = X(g.bar_x[i]);
            dl->AddRectFilled(ImVec2(x - bw / 2, Y(g.bar_y[i])), ImVec2(x + bw / 2, main_b),
                              withAlpha(g.bar_y[i] > 1.0f ? IM_COL32(138, 79, 214, 255) : ui::MUTED, 0.7f));
        }
        for (const auto& c : g.players) {
            if (c.x.empty() || hidden(c)) continue;
            if (view.chart == GraphChart::Predicted) {
                for (std::size_t i = 0; i < c.x.size(); ++i) {
                    const float x = X(c.x[i]);
                    dl->AddRectFilled(ImVec2(x - bw / 2, Y(c.y[i])), ImVec2(x + bw / 2, main_b),
                                      withAlpha(c.colour, 0.8f));
                }
                continue;
            }
            if (!c.extra.empty()) {
                GraphCurve thin = c;
                thin.colour     = withAlpha(c.colour, 0.5f);
                draw_curve(thin, c.extra, Y, 1.0f);
            }
            draw_curve(c, c.y, Y, px(1.6f));
        }
    }

    // Move numbers along the bottom of the chart.
    {
        const float per_move = 2 * (right - left) / static_cast<float>(n);
        int step             = 5;
        while (step * per_move < px(34)) step *= 2;
        for (int mv = (g.first_move / step + 1) * step;; mv += step) {
            const int ply = (mv - g.first_move) * 2 + (g.black_first ? 0 : 1);
            if (ply > g.plies) break;
            const auto label = std::to_string(mv);
            const float w    = font->CalcTextSizeA(tiny, FLT_MAX, 0, label.c_str()).x;
            dl->AddText(font, tiny, ImVec2(X(static_cast<float>(ply)) - w / 2, main_b - tiny - px(1)),
                        withAlpha(ui::MUTED, 0.75f), label.c_str());
        }
    }

    // The phases under the chart.
    {
        const int starts[3] = {0, g.phases.middlegame, g.phases.endgame};
        const char* names[] = {TR("Opening"), TR("Middlegame"), TR("Endgame")};
        for (int i = 0; i < 3; ++i) {
            if (starts[i] < 0 || starts[i] > g.plies) continue;
            int end = g.plies;
            for (int k = i + 1; k < 3; ++k)
                if (starts[k] >= 0) {
                    end = starts[k];
                    break;
                }
            if (end <= starts[i] && i < 2) continue;
            const float x0 = X(static_cast<float>(starts[i])), x1 = X(static_cast<float>(std::max(end, starts[i])));
            if (i > 0) dl->AddLine(ImVec2(x0, axis_y - axis_h * 0.45f), ImVec2(x0, axis_y + axis_h * 0.45f), ui::MUTED);
            const float w = font->CalcTextSizeA(tiny, FLT_MAX, 0, names[i]).x;
            if (x1 - x0 > w + px(8))
                dl->AddText(font, tiny, ImVec2((x0 + x1 - w) / 2, axis_y - tiny / 2), ui::MUTED, names[i]);
        }
    }

    // The time strip: White's moves up, Black's down.
    if (strip) {
        const float mid = (strip_t + strip_b) / 2, half = (strip_b - strip_t) / 2 - px(1);
        dl->AddRectFilled(ImVec2(left, strip_t), ImVec2(right, strip_b), plotBg());
        float hi = 0;
        for (const auto& s : g.time_s)
            for (const float v : s) hi = std::max(hi, v);
        const float scale = niceMax(hi);
        const float bw    = std::max(1.0f, (right - left) / static_cast<float>(n) - 1.0f);
        for (int side = 0; side < 2; ++side) {
            const auto& xs = g.time_x[static_cast<std::size_t>(side)];
            const auto& ss = g.time_s[static_cast<std::size_t>(side)];
            for (std::size_t i = 0; i < xs.size(); ++i) {
                const float x = X(xs[i]), h = half * std::clamp(ss[i] / scale, 0.0f, 1.0f);
                if (side == 0) {
                    dl->AddRectFilled(ImVec2(x - bw / 2, mid - h), ImVec2(x + bw / 2, mid), whiteSide());
                    if (light())
                        dl->AddRect(ImVec2(x - bw / 2, mid - h), ImVec2(x + bw / 2, mid), withAlpha(ui::MUTED, 0.5f));
                } else {
                    dl->AddRectFilled(ImVec2(x - bw / 2, mid), ImVec2(x + bw / 2, mid + h), blackSide());
                    // In the dark theme Black's bars are as dark as the strip: outlined, as White's are in the light.
                    if (!light())
                        dl->AddRect(ImVec2(x - bw / 2, mid), ImVec2(x + bw / 2, mid + h), withAlpha(ui::MUTED, 0.8f));
                }
            }
        }
        dl->AddLine(ImVec2(left, mid), ImVec2(right, mid), withAlpha(ui::MUTED, 0.6f));
        const auto label = unitText(GraphChart::Time, scale);
        const float w    = font->CalcTextSizeA(tiny, FLT_MAX, 0, label.c_str()).x;
        dl->AddText(font, tiny, ImVec2(left - px(4) - w, strip_t), ui::MUTED, label.c_str());
        dl->AddText(font, tiny, ImVec2(left - px(4) - w, strip_b - tiny), ui::MUTED, label.c_str());
        const char* what = TR("time per move · White up, Black down");
        const float ww   = font->CalcTextSizeA(tiny, FLT_MAX, 0, what).x;
        dl->AddText(font, tiny, ImVec2(right - ww - px(4), strip_t + px(1)), withAlpha(ui::MUTED, 0.85f), what);
    }

    // What each position got from the continuous analysis.
    if (!g.level.empty()) {
        const float y = (strip ? strip_b : main_b + axis_h) + px(3);
        const float w = (right - left) / static_cast<float>(n);
        for (std::size_t p = 0; p < g.level.size() && static_cast<int>(p) <= g.plies; ++p) {
            const float x = X(static_cast<float>(p));
            dl->AddRectFilled(ImVec2(x - w / 2, y), ImVec2(x + w / 2 + 0.5f, y + px(6)),
                              withAlpha(ui::ACCENT, 0.1f + 0.8f * std::clamp(g.level[p], 0.0f, 1.0f)));
        }
    }

    // The position shown, and the one under the mouse.
    const float cursor_b = strip ? strip_b : main_b;
    if (d.current >= 0 && d.current <= g.plies) {
        const float x = X(static_cast<float>(d.current));
        dl->AddLine(ImVec2(x, main_t), ImVec2(x, cursor_b), ui::ACCENT, px(1.5f));
    }
    if (hovered) {
        const float x = X(static_cast<float>(hover_ply));
        dl->AddLine(ImVec2(x, main_t), ImVec2(x, cursor_b), withAlpha(text, 0.25f));
    }
    dl->PopClipRect();

    ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopID();
    return ev;
}

}  // namespace fcg
