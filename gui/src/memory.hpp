#pragma once

// The memory a tournament's hash tables take, against what the machine has.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <tour/model.hpp>

#include "model.hpp"
#include "topology.hpp"

namespace fcg {

// The machine's memory, in MB: installed, and available to new programs without swapping.
struct SystemMemory {
    uint64_t total_mb     = 0;
    uint64_t available_mb = 0;
};

// Asks the system (/proc/meminfo on Linux, GlobalMemoryStatusEx on Windows); nullopt when it cannot tell.
[[nodiscard]] std::optional<SystemMemory> systemMemory();

// "512 MB", "1.5 GB".
[[nodiscard]] std::string formatMemory(uint64_t mb);

// The hash tables of a tournament's engine processes: the most memory they hold at once.
struct MemoryNeed {
    uint64_t hash_mb = 0;
    int processes    = 0;  // engine processes in memory at most

    std::optional<SystemMemory> system;  // the machine's, when known

    [[nodiscard]] bool aboveInstalled() const { return system && hash_mb > system->total_mb; }
    [[nodiscard]] bool aboveAvailable() const { return system && hash_mb > system->available_mb; }
    // What the setup summary, the start and the console say: the need, and how it compares.
    [[nodiscard]] std::string text() const;
};

// The engine processes fastchess keeps (two per game being played, plus the idle ones waiting for their next game,
// see BaseTournament::engineCacheSize()) with the Hash of each engine, `hashes` one per participant in MB. An
// engine has at most one process per game played at once: the largest hashes fill the processes kept.
[[nodiscard]] MemoryNeed hashMemory(const std::vector<uint64_t>& hashes, int concurrency, bool affinity);

// The Hash of every participant of `setup` as it will play: its own, else the common one, else the engine's
// setting, else the engine's default (0 for an engine without a Hash option, or not read yet).
[[nodiscard]] std::vector<uint64_t> setupHashes(const MatchSetup& setup, const std::vector<EngineEntry>& engines);

// The same for a tournament created already: the Hash its participants were given, else the default of the engine
// of the same name in `engines`.
[[nodiscard]] std::vector<uint64_t> specHashes(const tour::TournamentSpec& spec,
                                               const std::vector<EngineEntry>& engines);

// hashMemory() of a setup, with its concurrency as buildRun() makes it, and the machine's memory.
[[nodiscard]] MemoryNeed setupMemory(const MatchSetup& setup, const std::vector<EngineEntry>& engines,
                                     const CpuTopology& topology);

// hashMemory() of a tournament created already, and the machine's memory.
[[nodiscard]] MemoryNeed specMemory(const tour::TournamentSpec& spec, const std::vector<EngineEntry>& engines);

}  // namespace fcg
