#pragma once

// What the games of a tournament take of the machine (engine threads, memory, GPUs), what a tournament may use of it
// (its allowance), and how the window shares the machine between the tournaments running at once (share()).
//
// Each tournament plays in a process of its own (fastchess keeps a tournament in globals); the window gives each one an
// allowance, and the runner starts a game only when the games in flight and this one fit it (Runner::setAllowance()).
// Lowering an allowance never interrupts a game: the runner applies it at its next game.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <types/engine_config.hpp>

#include "model.hpp"

namespace fcg::tour {

// The Hash an engine plays with when its configuration does not say: the usual default of UCI engines.
inline constexpr uint64_t DEFAULT_HASH_MB = 16;

// What an engine process takes besides its Hash: its program, its other tables, its threads' stacks and search data.
// A guess on the safe side; an engine with a network (NNUE) takes about this much more.
inline constexpr uint64_t PROCESS_MB = 64;
inline constexpr uint64_t THREAD_MB  = 8;

// The Threads and Hash an engine configuration plays with: its options, else 1 and DEFAULT_HASH_MB.
[[nodiscard]] int engineThreads(const fastchess::EngineConfiguration& engine);
[[nodiscard]] uint64_t engineHashMb(const fastchess::EngineConfiguration& engine);

// The memory one engine process takes: its Hash, PROCESS_MB and THREAD_MB per thread.
[[nodiscard]] uint64_t processMb(const fastchess::EngineConfiguration& engine);

// What a game takes while it is played.
struct GameCost {
    // The engine threads it keeps busy: the Threads of the larger of its two engines, as one thinks while the other
    // waits (ponder is off), the way the setup counts threads against cores. A game of two 4-thread engines takes 4.
    int threads        = 0;
    uint64_t memory_mb = 0;  // the memory of their two processes (processMb())
    int gpus           = 0;  // its engines that use a GPU (Participant::gpu)

    GameCost& operator+=(const GameCost& o) {
        threads += o.threads;
        memory_mb += o.memory_mb;
        gpus += o.gpus;
        return *this;
    }
    GameCost& operator-=(const GameCost& o) {
        threads -= o.threads;
        memory_mb = memory_mb > o.memory_mb ? memory_mb - o.memory_mb : 0;
        gpus -= o.gpus;
        return *this;
    }
    bool operator==(const GameCost&) const = default;
};

// The cost of a game between two participants.
[[nodiscard]] GameCost gameCost(const Participant& a, const Participant& b);

// The cost of the heaviest game of a tournament's active participants: what one game at a time needs.
[[nodiscard]] GameCost largestGame(const TournamentSpec& spec);

// What held the last game back: the resource it waits for.
enum class Wait { None, Cores, Memory, Gpus };

// What a tournament may use now; -1 is no limit (a tournament alone, run from a terminal), 0 lets no game start.
struct Allowance {
    int threads       = -1;
    int64_t memory_mb = -1;
    int gpus          = -1;
    // A game may start when none is in flight, though it does not fit the threads or the memory: a tournament alone
    // (or the first of the window's) always plays. Never beyond the GPUs, which games do not share.
    bool one_game_always = true;

    // Whether `game` may start with `in_flight` (`games` of them) being played; Wait::None when it may, else the
    // resource it waits for.
    [[nodiscard]] Wait admits(const GameCost& in_flight, int games, const GameCost& game) const;

    bool operator==(const Allowance&) const = default;
};

// A tournament the window shares the machine with others: what share() needs to know of it.
struct Claim {
    bool active   = true;  // running and not paused: a paused tournament gets nothing, it starts no game
    double weight = 1;     // its share against the others'
    GameCost game;         // its heaviest game (largestGame())
    int ceiling = 1;       // the games it plays at once at most: its concurrency
};

// The machine's budget, as Settings has it: engine threads (cores), memory and GPUs (0: no limit).
struct Budget {
    int threads        = 1;
    uint64_t memory_mb = 0;
    int gpus           = 0;
};

// Shares the budget between the tournaments by water-filling, in engine threads, then memory, then GPUs alike: each
// active tournament gets at least one game's worth while the budget allows (in their order: the first one always gets
// it, beyond the budget if it must), then what is left goes to them by weight, none above its ceiling (its concurrency
// times its heaviest game); what a tournament cannot take goes to the others. An inactive tournament gets nothing.
[[nodiscard]] std::vector<Allowance> share(const Budget& budget, const std::vector<Claim>& claims);

// The games a budget lets a tournament play at once, its heaviest game each, at most `ceiling`: how many games the
// setup summary says the machine allows. 0 limits mean no limit.
[[nodiscard]] int gamesAllowed(const Budget& budget, const GameCost& game, int ceiling);

}  // namespace fcg::tour
