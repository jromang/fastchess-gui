#include "pairing.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace fcg::tour {

namespace {

std::pair<int, int> key(int a, int b) { return {std::min(a, b), std::max(a, b)}; }

}  // namespace

RoundPairing swissPairing(const std::vector<int>& players, const std::map<int, double>& score,
                          const std::set<std::pair<int, int>>& played, const std::set<int>& had_bye) {
    RoundPairing out;

    // Ranking: score, then seed.
    std::map<int, std::size_t> seed;
    for (std::size_t i = 0; i < players.size(); ++i) seed[players[i]] = i;
    const auto points = [&](int id) {
        const auto it = score.find(id);
        return it == score.end() ? 0.0 : it->second;
    };
    std::vector<int> ranked = players;
    std::stable_sort(ranked.begin(), ranked.end(), [&](int a, int b) {
        if (points(a) != points(b)) return points(a) > points(b);
        return seed[a] < seed[b];
    });

    // The bye: the lowest ranked player who has not had one.
    if (ranked.size() % 2 == 1) {
        auto pos = ranked.size() - 1;
        for (std::size_t i = ranked.size(); i-- > 0;) {
            if (!had_bye.count(ranked[i])) {
                pos = i;
                break;
            }
        }
        out.byes.push_back(ranked[pos]);
        ranked.erase(ranked.begin() + static_cast<std::ptrdiff_t>(pos));
    }

    const auto n = ranked.size();
    std::vector<bool> taken(n, false);
    std::vector<std::pair<int, int>> pairs;

    // The ideal opponent of the i-th player is half a score group further down (Dutch style): the candidates are
    // tried by score difference, then by distance to that ideal place.
    const auto candidates = [&](std::size_t i) {
        std::size_t group_end = i;
        while (group_end < n && points(ranked[group_end]) == points(ranked[i])) ++group_end;
        std::size_t group_start = i;
        while (group_start > 0 && points(ranked[group_start - 1]) == points(ranked[i])) --group_start;
        const auto half  = (group_end - group_start) / 2;
        const auto ideal = static_cast<double>(i + std::max<std::size_t>(1, half));

        std::vector<std::size_t> list;
        for (std::size_t j = i + 1; j < n; ++j) {
            if (!taken[j]) list.push_back(j);
        }
        std::stable_sort(list.begin(), list.end(), [&](std::size_t a, std::size_t b) {
            const double da = std::abs(points(ranked[a]) - points(ranked[i]));
            const double db = std::abs(points(ranked[b]) - points(ranked[i]));
            if (da != db) return da < db;
            return std::abs(static_cast<double>(a) - ideal) < std::abs(static_cast<double>(b) - ideal);
        });
        return list;
    };

    // Depth-first search for a pairing without rematches; the steps are bounded so a hopeless case gives up
    // quickly and allows rematches instead.
    long steps                             = 0;
    const std::function<bool(bool)> search = [&](bool allow_rematch) -> bool {
        std::size_t i = 0;
        while (i < n && taken[i]) ++i;
        if (i == n) return true;
        if (++steps > 200000) return false;
        taken[i] = true;
        for (const auto j : candidates(i)) {
            if (!allow_rematch && played.count(key(ranked[i], ranked[j]))) continue;
            taken[j] = true;
            pairs.emplace_back(ranked[i], ranked[j]);
            if (search(allow_rematch)) return true;
            pairs.pop_back();
            taken[j] = false;
        }
        taken[i] = false;
        return false;
    };

    if (!search(false)) {
        std::fill(taken.begin(), taken.end(), false);
        pairs.clear();
        steps = 0;
        search(true);
    }
    out.pairs = std::move(pairs);
    return out;
}

std::vector<int> bracketOrder(int size) {
    std::vector<int> order = {1};
    while (static_cast<int>(order.size()) < size) {
        const int next = static_cast<int>(order.size()) * 2 + 1;
        std::vector<int> grown;
        for (const int s : order) {
            grown.push_back(s);
            grown.push_back(next - s);
        }
        order = std::move(grown);
    }
    return order;
}

RoundPairing knockoutFirstRound(const std::vector<int>& players) {
    RoundPairing out;
    const int n = static_cast<int>(players.size());
    int size    = 1;
    while (size < n) size *= 2;

    const auto order = bracketOrder(size);
    for (std::size_t i = 0; i + 1 < order.size(); i += 2) {
        const int a = order[i], b = order[i + 1];  // seeds, 1-based; a seed beyond n is a bye
        const int top = players[static_cast<std::size_t>(std::min(a, b) - 1)];
        if (a <= n && b <= n) {
            const int bottom = players[static_cast<std::size_t>(std::max(a, b) - 1)];
            out.pairs.emplace_back(top, bottom);
            out.bracket.push_back({top, bottom});
        } else {
            out.byes.push_back(top);
            out.bracket.push_back({top});
        }
    }
    return out;
}

}  // namespace fcg::tour
