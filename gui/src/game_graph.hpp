#pragma once

// The graph of a game (Board page, Games page, Live view): the evaluations of the two engines that played it and of
// the analyses made of it, the moves' judgements, the book, the phases and the tablebase zone, and under its axis the
// time each move took. buildGameGraph() reads it from the game, drawGameGraph() draws it with its tabs and legend.

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <imgui.h>
#include <json.hpp>

#include "game_analysis.hpp"
#include "pgn_store.hpp"

namespace fcg {

// What the graph shows above its time strip.
enum class GraphChart : uint8_t { Eval, Win, Wdl, Time, Depth, Nodes, Speed, TbHits, Agreement, Predicted, Count };
NLOHMANN_JSON_SERIALIZE_ENUM(GraphChart, {{GraphChart::Eval, "eval"},
                                          {GraphChart::Win, "win"},
                                          {GraphChart::Wdl, "wdl"},
                                          {GraphChart::Time, "time"},
                                          {GraphChart::Depth, "depth"},
                                          {GraphChart::Nodes, "nodes"},
                                          {GraphChart::Speed, "speed"},
                                          {GraphChart::TbHits, "tbhits"},
                                          {GraphChart::Agreement, "agreement"},
                                          {GraphChart::Predicted, "predicted"}})

// The phases of a game, as lichess divides them: the middlegame from the first position with at most 10 pieces other
// than kings and pawns, or with fewer than 4 pieces on one side's back rank; the endgame from the first position
// with at most 6. Each is the ply of that position (the position after that many moves), -1 when not reached.
struct GamePhases {
    int middlegame = -1;
    int endgame    = -1;
};
[[nodiscard]] GamePhases gamePhases(const ReplayGame& game, bool chess960);

enum class Phase : uint8_t { Opening, Middlegame, Endgame };
[[nodiscard]] Phase phaseAt(const GamePhases& phases, int ply);

// The win, draw and loss shares engines reported (UCI_ShowWDL), from White's point of view, after the plies that
// have them.
struct WdlSeries {
    std::vector<float> ply;
    std::vector<float> win;       // White wins
    std::vector<float> not_lost;  // White wins or draws
    [[nodiscard]] bool empty() const noexcept { return ply.empty(); }
};
template <typename Moves>
WdlSeries wdlSeries(const Moves& moves) {
    WdlSeries s;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        const auto& m = moves[i];
        if (m.book || !m.wdl) continue;
        const bool white = m.mover == chess::Color::WHITE;
        const float w = static_cast<float>((*m.wdl)[white ? 0 : 2]), d = static_cast<float>((*m.wdl)[1]),
                    l   = static_cast<float>((*m.wdl)[white ? 2 : 0]);
        const float sum = std::max(1.0f, w + d + l);
        s.ply.push_back(static_cast<float>(i + 1));
        s.win.push_back(w / sum);
        s.not_lost.push_back((w + d) / sum);
    }
    return s;
}

// One engine's values at the positions it has them: x is the ply (the position after x moves), y White's pawns for
// the evaluation (beyond EVAL_SCALE when it is out of the graph: a mate is ±MATE_PAWNS), the chart's unit otherwise.
inline constexpr float EVAL_SCALE = 6.0f;
inline constexpr float MATE_PAWNS = 100.0f;
struct GraphCurve {
    std::string key;  // what GraphView::hidden keeps: "white", "black", or "analysis:" and the analysis's name
    std::string name;
    std::string note;  // beside the name in the legend
    ImU32 colour = 0;
    bool dashed  = false;
    std::vector<float> x, y;
    std::vector<float> win;    // White's expected score (0 to 1) at the same x: from the WDL, or the lichess formula
    std::vector<float> extra;  // the thinner line at the same x (seldepth, decision time); NaN where none
};

struct GraphMark {
    int ply             = 0;  // the position after the move
    Judgement judgement = Judgement::None;
    bool hollow         = false;  // not settled: the analyses disagree, or it changed at the last pass
};

struct GameGraph {
    int plies        = 0;
    int first_move   = 1;      // the number of the game's first move
    bool black_first = false;  // the game starts with Black to move
    std::vector<bool> book;    // per move: move `ply` is book[ply - 1]
    GamePhases phases;
    int tablebase = -1;  // the first position the tablebases know the result of, -1 when none
    // The chart's values: the two engines that played (White's, Black's), at their own moves; for the evaluation,
    // the analyses too, each at every position it searched, the judging one first.
    std::array<GraphCurve, 2> players;
    std::vector<GraphCurve> analyses;
    std::vector<GraphMark> marks;  // the judging analysis's
    // Bars at some positions: how far the analyses are apart (Agreement).
    std::vector<float> bar_x, bar_y;
    // The win, draw and loss shares the players reported (Wdl).
    WdlSeries wdl;
    // The time strip: each side's moves, their ply and seconds.
    std::array<std::vector<float>, 2> time_x, time_s;
    // What each position got from the continuous analysis, 0 to 1 (darker is more); empty when none runs.
    std::vector<float> level;
    // The charts the game has values for.
    std::array<bool, static_cast<std::size_t>(GraphChart::Count)> available{};
};

// How many plies the PV of a move repeats of what the opponent's previous PV predicted after its own move.
[[nodiscard]] int pvAgreement(const std::string& previous_pv, const std::string& pv);

// White's pawns from a score of the side `stm` (a mate ±MATE_PAWNS), NaN without one.
[[nodiscard]] float graphPawns(const std::optional<fastchess::Score>& score, chess::Color stm);
// White's expected score from the side's WDL (permille), or from its score with the lichess formula; NaN without
// either.
[[nodiscard]] float graphWin(const std::optional<fastchess::Score>& score,
                             const std::optional<std::array<int64_t, 3>>& wdl, chess::Color stm);

// One analysis of the game for the graph: its positions (plies + 1 of them, or fewer while it runs).
struct GraphAnalysis {
    std::string name;
    std::string note;
    ImU32 colour             = 0;
    const GameReview* review = nullptr;
};

// The graph of `game` for `chart`: the engines' own values read from its moves, `analyses` for the evaluation (the
// first judges, with `judged` its moves' judgements).
[[nodiscard]] GameGraph buildGameGraph(const ReplayGame& game, bool chess960, GraphChart chart,
                                       const std::vector<GraphAnalysis>& analyses,
                                       const std::vector<MoveReview>& judged);

// What the user chose on a graph, kept by its page.
struct GraphView {
    GraphChart chart = GraphChart::Eval;
    std::vector<std::string> hidden;  // the curves hidden from the legend (GraphCurve::key)
    bool folded = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GraphView, chart, hidden, folded)

// What happened on the graph this frame.
struct GraphEvents {
    int go_to         = -1;     // the ply clicked or dragged to
    bool view_changed = false;  // GraphView changed (to be saved)
    bool add_analysis = false;  // "+" in the legend
    int judge         = -1;     // an analysis to judge the moves with, by index
    int remove        = -1;     // an analysis to remove, by index
    bool pgn_options  = false;  // the setup's PGN options asked for (a chart its PGN lacks)
    bool action       = false;  // GraphDraw::action clicked
};

struct GraphDraw {
    int current = -1;  // the position shown, marked by a line
    // The tooltip of a ply (the position after it); none when empty.
    std::function<std::string(int)> tip;
    std::string progress;        // in the header, right: the continuous analysis's state
    bool can_add       = false;  // "+" in the legend
    bool analyses_menu = false;  // the analyses' legend items have a menu (judge, remove)
    // Why a chart has no values, for the charts menu (the PGN options that write them); none: such charts are left out.
    std::function<std::string(GraphChart)> missing;
    bool pgn_options = false;  // with them, a button to the setup's PGN options
    // A button in the header, right, beside the progress (the continuous analysis's start or stop).
    const char* action     = nullptr;
    const char* action_tip = nullptr;
};

// Draws the graph in `size` (its header, legend, chart, phases and time strip); folded, only its header.
GraphEvents drawGameGraph(const char* id, const GameGraph& graph, GraphView& view, const GraphDraw& draw, ImVec2 size);
// The height of a folded graph: its header line.
[[nodiscard]] float foldedGraphHeight();

// The graph's tooltip at a move of `game` (the position after `ply` moves): the move, with `mark` after it, and what
// the engine that played it said. Empty at the start position.
[[nodiscard]] std::string graphMoveTip(const ReplayGame& game, int ply, const char* mark = "");

// The chart's name in the tabs.
[[nodiscard]] const char* graphChartName(GraphChart chart);

}  // namespace fcg
