#include "review_stats.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

#include <fmt/include/fmt/format.h>

#include "board_page.hpp"
#include "board_text.hpp"
#include "live.hpp"

namespace fcg {

namespace {

// Centipawns of the side to move, a mate as ±1000, within ±1000.
std::optional<double> cpOf(const std::optional<fastchess::Score>& score) {
    if (!score) return std::nullopt;
    if (score->type == fastchess::ScoreType::MATE) return score->value > 0 ? 1000.0 : -1000.0;
    if (score->type != fastchess::ScoreType::CP) return std::nullopt;
    return std::clamp(static_cast<double>(score->value), -1000.0, 1000.0);
}

double stdDev(const std::vector<double>& xs) {
    if (xs.empty()) return 0;
    const double mean = std::accumulate(xs.begin(), xs.end(), 0.0) / static_cast<double>(xs.size());
    double sum        = 0;
    for (const double x : xs) sum += (x - mean) * (x - mean);
    return std::sqrt(sum / static_cast<double>(xs.size()));
}

// lichess: the mean of the volatility-weighted mean and the harmonic mean of the moves' accuracies.
std::optional<double> combine(const std::vector<double>& accuracy, const std::vector<double>& weight) {
    if (accuracy.empty()) return std::nullopt;
    double weighted = 0, weights = 0, inverse = 0;
    for (std::size_t i = 0; i < accuracy.size(); ++i) {
        weighted += accuracy[i] * weight[i];
        weights += weight[i];
        inverse += 1.0 / std::max(accuracy[i], 1.0);
    }
    const double harmonic = static_cast<double>(accuracy.size()) / inverse;
    return (weighted / std::max(weights, 1e-9) + harmonic) / 2;
}

}  // namespace

double moveAccuracy(double before, double after) {
    const double loss = std::max(0.0, before - after);
    return std::clamp(103.1668 * std::exp(-0.04354 * loss) - 3.1669, 0.0, 100.0);
}

std::array<SideStats, 2> gameStats(const ReplayGame& game, bool chess960, const GameReview& review,
                                   const std::vector<MoveReview>& judged, const GamePhases& phases) {
    std::array<SideStats, 2> out;
    const auto n = std::min(game.moves.size(), review.positions.size() > 0 ? review.positions.size() - 1 : 0);
    // White's winning chances at each position, for the weights of the moves: how much the game swings around
    // them (lichess: windows of n/10 plies, 2 to 8).
    std::vector<double> white(n + 1, 50.0);
    for (std::size_t p = 0; p <= n; ++p) {
        const auto& s = review.positions[p].score;
        if (!s) continue;
        const double own = winChance(*s);
        const bool w = p < game.moves.size() ? game.moves[p].mover == chess::Color::WHITE
                                             : !game.moves.empty() && game.moves.back().mover == chess::Color::BLACK;
        white[p]     = w ? own : 100.0 - own;
    }
    const std::size_t window = std::clamp<std::size_t>(n / 10, 2, 8);
    std::vector<double> weights(n, 1.0);
    for (std::size_t i = 0; i < n; ++i) {
        // The window ending after the move, the first ones the same as the first full window.
        const std::size_t end   = std::min(white.size(), std::max(i + 2, window));
        const std::size_t begin = end >= window ? end - window : 0;
        weights[i] = std::clamp(stdDev(std::vector<double>(white.begin() + static_cast<std::ptrdiff_t>(begin),
                                                           white.begin() + static_cast<std::ptrdiff_t>(end))),
                                0.5, 12.0);
    }

    std::array<std::vector<double>, 2> acc, wts;
    std::array<std::array<std::vector<double>, 3>, 2> phase_acc, phase_wts;
    std::array<double, 2> loss{}, agree{}, depth{};
    std::array<int, 2> depth_n{}, agree_n{};
    for (std::size_t i = 0; i < game.moves.size(); ++i) {
        const auto& m  = game.moves[i];
        const int side = m.mover == chess::Color::WHITE ? 0 : 1;
        auto& st       = out[static_cast<std::size_t>(side)];
        if (m.time_ms > 0) st.time_ms += m.time_ms;
        if (!m.book && m.depth > 0) {
            depth[static_cast<std::size_t>(side)] += m.depth;
            depth_n[static_cast<std::size_t>(side)]++;
        }
        if (m.book || i >= n) continue;
        const auto& before = review.positions[i];
        const auto& after  = review.positions[i + 1];
        const auto b = cpOf(before.score), a = cpOf(after.score);
        if (!b || !a) continue;
        st.moves++;
        const double was = winChance(*before.score), is = 100.0 - winChance(*after.score);
        const double accuracy = moveAccuracy(was, is);
        acc[static_cast<std::size_t>(side)].push_back(accuracy);
        wts[static_cast<std::size_t>(side)].push_back(weights[i]);
        const auto phase = static_cast<std::size_t>(phaseAt(phases, static_cast<int>(i)));
        phase_acc[static_cast<std::size_t>(side)][phase].push_back(accuracy);
        phase_wts[static_cast<std::size_t>(side)][phase].push_back(weights[i]);
        loss[static_cast<std::size_t>(side)] += std::clamp(*b + *a, 0.0, 1000.0);  // the mover's before less after
        if (!before.best.empty()) {
            agree_n[static_cast<std::size_t>(side)]++;
            if (before.best == chess::uci::moveToUci(m.move, chess960)) agree[static_cast<std::size_t>(side)]++;
        }
        if (i < judged.size() && judged[i].judgement != Judgement::None)
            st.judgements[static_cast<std::size_t>(judged[i].judgement) - 1]++;
    }
    for (std::size_t side = 0; side < 2; ++side) {
        auto& st    = out[side];
        st.accuracy = combine(acc[side], wts[side]);
        if (st.moves > 0) st.average_loss = loss[side] / st.moves;
        if (agree_n[side] > 0) st.agreement = agree[side] / agree_n[side];
        if (depth_n[side] > 0) st.depth = depth[side] / depth_n[side];
        for (std::size_t p = 0; p < 3; ++p) st.phase_accuracy[p] = combine(phase_acc[side][p], phase_wts[side][p]);
    }
    return out;
}

std::vector<KeyMoment> keyMoments(const ReplayGame& game, const GameReview& review,
                                  const std::vector<MoveReview>& judged, const GamePhases& phases, int tablebase) {
    std::vector<KeyMoment> out;
    const auto white_pawns = [&](std::size_t p) {
        if (p >= review.positions.size()) return std::nanf("");
        const auto stm = p < game.moves.size() ? game.moves[p].mover
                                               : (game.moves.empty() ? chess::Color::WHITE : ~game.moves.back().mover);
        return graphPawns(review.positions[p].score, stm);
    };
    // The mistakes and blunders, the one that lost the most first among them.
    std::size_t worst = judged.size();
    for (std::size_t i = 0; i < judged.size(); ++i) {
        if (judged[i].judgement < Judgement::Mistake) continue;
        if (worst == judged.size() || judged[i].loss > judged[worst].loss) worst = i;
    }
    for (std::size_t i = 0; i < judged.size(); ++i) {
        if (judged[i].judgement < Judgement::Mistake) continue;
        KeyMoment k;
        k.kind      = i == worst ? KeyMoment::Kind::TurningPoint : KeyMoment::Kind::Judged;
        k.ply       = static_cast<int>(i) + 1;
        k.before    = white_pawns(i);
        k.after     = white_pawns(i + 1);
        k.judgement = judged[i].judgement;
        out.push_back(k);
    }
    // The two engines apart by 1.5 pawns or more, two moves in a row: where it starts.
    int found = 0, run = 0;
    for (std::size_t i = 1; i < game.moves.size() && found < 3; ++i) {
        const auto &m = game.moves[i], &prev = game.moves[i - 1];
        const float a    = m.book ? std::nanf("") : graphPawns(m.score, m.mover);
        const float b    = prev.book ? std::nanf("") : graphPawns(prev.score, prev.mover);
        const bool apart = std::isfinite(a) && std::isfinite(b) &&
                           std::fabs(std::clamp(a, -10.0f, 10.0f) - std::clamp(b, -10.0f, 10.0f)) >= 1.5f;
        run              = apart ? run + 1 : 0;
        if (run == 2) {
            out.push_back({KeyMoment::Kind::Disagreement, static_cast<int>(i), a, b, Judgement::None});
            ++found;
        }
    }
    if (phases.endgame > 0) out.push_back({KeyMoment::Kind::Endgame, phases.endgame, 0, 0, Judgement::None});
    if (tablebase >= 0) out.push_back({KeyMoment::Kind::Tablebase, tablebase, 0, 0, Judgement::None});
    std::stable_sort(out.begin(), out.end(), [](const auto& x, const auto& y) { return x.ply < y.ply; });
    return out;
}

int sawItAt(const GraphCurve& curve, int ply, int sign) {
    for (std::size_t i = 0; i < curve.x.size(); ++i) {
        if (curve.x[i] < static_cast<float>(ply - 12)) continue;
        if (curve.y[i] * static_cast<float>(sign) >= 1.0f) return static_cast<int>(curve.x[i]);
    }
    return -1;
}

std::string pgnWithAnalysis(const GameTree& tree, bool chess960, const GameReview& review,
                            const std::vector<MoveReview>& judged) {
    const auto& game = tree.main;
    return gamePgn(tree, chess960, [&](int ply) {
        std::string out;
        const auto i = static_cast<std::size_t>(ply - 1);
        if (i < judged.size()) {
            switch (judged[i].judgement) {
                case Judgement::Inaccuracy:
                    out += "$6";
                    break;
                case Judgement::Mistake:
                    out += "$2";
                    break;
                case Judgement::Blunder:
                    out += "$4";
                    break;
                default:
                    break;
            }
        }
        if (static_cast<std::size_t>(ply) < review.positions.size()) {
            const auto& pos = review.positions[static_cast<std::size_t>(ply)];
            const auto stm  = static_cast<std::size_t>(ply) < game.moves.size()
                                  ? game.moves[static_cast<std::size_t>(ply)].mover
                                  : ~game.moves[i].mover;
            if (pos.score) {
                std::string eval;
                if (pos.score->type == fastchess::ScoreType::MATE) {
                    const auto v = stm == chess::Color::WHITE ? pos.score->value : -pos.score->value;
                    eval         = fmt::format("#{}", v);
                } else {
                    eval = fmt::format("{:.2f}", graphPawns(pos.score, stm));
                }
                std::string comment = fmt::format("[%eval {}]", eval);
                // After a judged move, the line the analysis preferred.
                if (i < judged.size() && judged[i].judgement != Judgement::None && i < review.positions.size() &&
                    !review.positions[i].pv.empty()) {
                    std::vector<std::string> pv;
                    std::istringstream in(review.positions[i].pv);
                    for (std::string w; in >> w;) pv.push_back(w);
                    comment +=
                        " " + review.engine + ": " + pvToSan(boardAt(game, chess960, static_cast<int>(i)), pv, 8);
                }
                out.append(out.empty() ? "" : " ").append("{").append(comment).append("}");
            }
        }
        return out;
    });
}

}  // namespace fcg
