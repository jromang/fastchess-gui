#include "unbalanced.hpp"

#include <algorithm>
#include <limits>
#include <string>

namespace fcg::tour {

int unbalancedOpening(const std::vector<PlyEval>& plies, const UnbalancedRule& rule) {
    if (rule.score_cp <= 0 || rule.moves <= 0) return 0;
    const auto wanted = static_cast<std::size_t>(rule.moves);

    // From White's point of view, the first moves of each engine after the book.
    std::vector<int64_t> evals[2];  // [0] White's moves, [1] Black's
    for (const auto& p : plies) {
        if (p.book) continue;
        auto& side = evals[p.white ? 0 : 1];
        if (side.size() >= wanted) continue;
        if (!p.score) return 0;
        int64_t own = p.score->value;
        if (p.score->type == fastchess::ScoreType::MATE) {
            own = p.score->value > 0 ? std::numeric_limits<int32_t>::max() : std::numeric_limits<int32_t>::min();
        } else if (p.score->type != fastchess::ScoreType::CP) {
            return 0;
        }
        side.push_back(p.white ? own : -own);
        if (evals[0].size() >= wanted && evals[1].size() >= wanted) break;
    }
    if (evals[0].size() < wanted || evals[1].size() < wanted) return 0;

    const auto all = [&](auto beyond) {
        return std::all_of(evals[0].begin(), evals[0].end(), beyond) &&
               std::all_of(evals[1].begin(), evals[1].end(), beyond);
    };
    if (all([&](int64_t v) { return v >= rule.score_cp; })) return 1;
    if (all([&](int64_t v) { return v <= -rule.score_cp; })) return -1;
    return 0;
}

int unbalancedOpening(const fastchess::MatchData& data, const UnbalancedRule& rule) {
    // The side to move of the starting position: the second field of its FEN.
    const auto space = data.fen.find(' ');
    bool white       = space == std::string::npos || data.fen.compare(space + 1, 1, "b") != 0;
    std::vector<PlyEval> plies;
    plies.reserve(data.moves.size());
    for (const auto& m : data.moves) {
        if (!m.legal) break;
        plies.push_back({white, m.book, m.book ? std::nullopt : m.score});
        white = !white;
    }
    return unbalancedOpening(plies, rule);
}

}  // namespace fcg::tour
