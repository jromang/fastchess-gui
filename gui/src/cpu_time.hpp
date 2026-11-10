#pragma once

// Processor time used by engine processes, to tell whether an engine uses more threads than it was given, or keeps
// thinking on its opponent's time.

#include <cstdint>
#include <vector>

namespace fcg {

// The processes whose processor time counts for an engine: its own and its descendants (the engine behind a
// WinBoard adapter, helper processes). Cheap enough once per game, not once per move.
[[nodiscard]] std::vector<int64_t> processTree(int64_t pid);

// Processor time (user and system) these processes used so far, in milliseconds; -1 when it cannot be read.
[[nodiscard]] int64_t cpuTimeMs(const std::vector<int64_t>& pids);

}  // namespace fcg
