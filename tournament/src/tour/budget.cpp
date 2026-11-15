#include "budget.hpp"

#include <algorithm>
#include <limits>

namespace fcg::tour {

namespace {

// The value of an option, nullopt when the configuration has none or it is no number.
std::optional<int64_t> numberOption(const fastchess::EngineConfiguration& engine, const char* name) {
    for (const auto& [key, value] : engine.options) {
        if (key != name) continue;
        try {
            return std::stoll(value);
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// What is left of a budget while it is shared: -1 is no limit.
struct Left {
    int64_t threads = -1;
    int64_t memory  = -1;
    int64_t gpus    = -1;

    [[nodiscard]] bool fits(const GameCost& c) const {
        return (threads < 0 || c.threads <= threads) && (memory < 0 || static_cast<int64_t>(c.memory_mb) <= memory) &&
               (gpus < 0 || c.gpus <= gpus);
    }
    void take(const GameCost& c) {
        if (threads >= 0) threads = std::max<int64_t>(0, threads - c.threads);
        if (memory >= 0) memory = std::max<int64_t>(0, memory - static_cast<int64_t>(c.memory_mb));
        if (gpus >= 0) gpus = std::max<int64_t>(0, gpus - c.gpus);
    }
};

}  // namespace

int engineThreads(const fastchess::EngineConfiguration& engine) {
    return static_cast<int>(std::clamp<int64_t>(numberOption(engine, "Threads").value_or(1), 1, 1 << 16));
}

uint64_t engineHashMb(const fastchess::EngineConfiguration& engine) {
    const auto hash = numberOption(engine, "Hash");
    return hash && *hash >= 0 ? static_cast<uint64_t>(*hash) : DEFAULT_HASH_MB;
}

uint64_t processMb(const fastchess::EngineConfiguration& engine) {
    return engineHashMb(engine) + PROCESS_MB + THREAD_MB * static_cast<uint64_t>(engineThreads(engine));
}

namespace {

// What a participant takes of this computer: nothing of its threads, hash and GPU when its engine runs on a machine,
// only its relay's process.
int localThreads(const Participant& p) { return p.remote ? 0 : engineThreads(p.engine); }
uint64_t localProcessMb(const Participant& p) { return p.remote ? PROCESS_MB : processMb(p.engine); }
bool localGpu(const Participant& p) { return p.gpu && !p.remote; }

}  // namespace

GameCost gameCost(const Participant& a, const Participant& b) {
    GameCost c;
    c.threads   = std::max(localThreads(a), localThreads(b));
    c.memory_mb = localProcessMb(a) + localProcessMb(b);
    c.gpus      = (localGpu(a) ? 1 : 0) + (localGpu(b) ? 1 : 0);
    return c;
}

GameCost largestGame(const TournamentSpec& spec) {
    // The two heaviest active participants of each kind: one game between them is the most a game takes.
    std::vector<int> threads;
    std::vector<uint64_t> memory;
    int gpus = 0;
    for (const auto& p : spec.participants) {
        if (!p.active) continue;
        threads.push_back(localThreads(p));
        memory.push_back(localProcessMb(p));
        gpus += localGpu(p) ? 1 : 0;
    }
    std::sort(threads.rbegin(), threads.rend());
    std::sort(memory.rbegin(), memory.rend());
    GameCost c;
    if (!threads.empty()) c.threads = threads[0];
    for (std::size_t i = 0; i < 2 && i < memory.size(); ++i) c.memory_mb += memory[i];
    c.gpus = std::min(gpus, 2);
    if (c.threads == 0) c.threads = 1;  // no participant yet, or only engines on machines: a thread for fastchess
    return c;
}

Wait Allowance::admits(const GameCost& in_flight, int games, const GameCost& game) const {
    const bool alone = games == 0 && one_game_always;
    if (gpus >= 0 && game.gpus > 0 && in_flight.gpus + game.gpus > gpus && !(alone && gpus >= 1)) return Wait::Gpus;
    if (threads >= 0 && in_flight.threads + game.threads > threads && !alone) return Wait::Cores;
    if (memory_mb >= 0 && in_flight.memory_mb + game.memory_mb > static_cast<uint64_t>(memory_mb) && !alone)
        return Wait::Memory;
    return Wait::None;
}

std::vector<Allowance> share(const Budget& budget, const std::vector<Claim>& claims) {
    std::vector<Allowance> out(claims.size());
    Left left;
    left.threads = std::max(1, budget.threads);
    left.memory  = budget.memory_mb > 0 ? static_cast<int64_t>(budget.memory_mb) : -1;
    left.gpus    = budget.gpus > 0 ? budget.gpus : -1;

    std::vector<int> games(claims.size(), 0);
    const auto costOf = [&](std::size_t i) {
        auto c    = claims[i].game;
        c.threads = std::max(1, c.threads);
        return c;
    };

    // One game each, in their order, while the budget allows; the first always gets it.
    bool first = true;
    for (std::size_t i = 0; i < claims.size(); ++i) {
        if (!claims[i].active || claims[i].ceiling < 1) continue;
        const auto cost = costOf(i);
        if (!first && !left.fits(cost)) continue;
        games[i] = 1;
        left.take(cost);
        out[i].one_game_always = first;
        first                  = false;
    }

    // The rest, a game at a time, to the tournament that has the fewest threads for its weight, among those below their
    // ceiling whose next game fits.
    for (;;) {
        std::size_t best = claims.size();
        double best_key  = std::numeric_limits<double>::max();
        for (std::size_t i = 0; i < claims.size(); ++i) {
            if (games[i] < 1 || games[i] >= claims[i].ceiling) continue;
            const auto cost = costOf(i);
            if (!left.fits(cost)) continue;
            const double key = static_cast<double>(games[i]) * cost.threads / std::max(1e-6, claims[i].weight);
            if (key < best_key) {
                best_key = key;
                best     = i;
            }
        }
        if (best == claims.size()) break;
        ++games[best];
        left.take(costOf(best));
    }

    for (std::size_t i = 0; i < claims.size(); ++i) {
        const auto cost = costOf(i);
        out[i].threads  = games[i] * cost.threads;
        out[i].memory_mb =
            budget.memory_mb > 0 ? static_cast<int64_t>(static_cast<uint64_t>(games[i]) * cost.memory_mb) : -1;
        out[i].gpus = budget.gpus > 0 ? std::min(budget.gpus, games[i] * cost.gpus) : -1;
        if (games[i] == 0) out[i].one_game_always = false;
    }
    // Threads that no whole game could take go to the tournaments in their order: a game lighter than the heaviest
    // one (participants with fewer threads) may use them.
    for (std::size_t i = 0; i < claims.size() && left.threads > 0; ++i) {
        if (games[i] < 1) continue;
        const int room = claims[i].ceiling * costOf(i).threads - out[i].threads;
        const int add  = static_cast<int>(std::min<int64_t>(room, left.threads));
        if (add <= 0) continue;
        out[i].threads += add;
        left.threads -= add;
    }
    return out;
}

int gamesAllowed(const Budget& budget, const GameCost& game, int ceiling) {
    int n = std::max(1, ceiling);
    if (budget.threads > 0 && game.threads > 0) n = std::min(n, budget.threads / game.threads);
    if (budget.memory_mb > 0 && game.memory_mb > 0)
        n = std::min<int>(n, static_cast<int>(budget.memory_mb / game.memory_mb));
    if (budget.gpus > 0 && game.gpus > 0) n = std::min(n, budget.gpus / game.gpus);
    return std::max(1, n);
}

}  // namespace fcg::tour
