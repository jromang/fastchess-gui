#include "big_screen.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace fcg {

int nextGame(const std::vector<BigCandidate>& games, int current) {
    const int n = static_cast<int>(games.size());
    for (int k = 1; k <= n; ++k) {
        const int i = ((current + k) % n + n) % n;
        if (games[static_cast<std::size_t>(i)].active) return i;
    }
    return current;
}

int mostInteresting(const std::vector<BigCandidate>& games, int current) {
    constexpr int BAND    = 30;  // centipawns
    constexpr int DECIDED = std::numeric_limits<int>::max() / 2;
    const auto closeness  = [](const BigCandidate& g) { return g.mate ? DECIDED : std::abs(g.eval_cp.value_or(0)); };

    int closest = DECIDED + 1;
    for (const auto& g : games) {
        if (g.active) closest = std::min(closest, closeness(g));
    }
    if (closest > DECIDED) return current;  // none is being played

    const auto close = [&](const BigCandidate& g) { return g.active && closeness(g) <= closest + BAND; };
    if (current >= 0 && current < static_cast<int>(games.size()) && close(games[static_cast<std::size_t>(current)])) {
        return current;
    }
    int best = -1;
    for (int i = 0; i < static_cast<int>(games.size()); ++i) {
        const auto& g = games[static_cast<std::size_t>(i)];
        if (close(g) && (best < 0 || g.plies > games[static_cast<std::size_t>(best)].plies)) best = i;
    }
    return best;
}

float bigTextSize(float width, float height, float scale) {
    // A 48th of the long side, a 27th of the short one at most: the same in landscape and in portrait.
    const float size = std::min(std::max(width, height) / 48.0f, std::min(width, height) / 27.0f);
    return std::clamp(size, 12.0f, 160.0f) * std::clamp(scale, 0.5f, 2.0f);
}

}  // namespace fcg
