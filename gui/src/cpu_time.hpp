#pragma once

// Processor time used by engine processes, to tell whether an engine uses more threads than it was given, or keeps
// thinking on its opponent's time.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace fcg {

// The processes whose processor time counts for an engine: its own and its descendants (the engine behind a
// WinBoard adapter, helper processes). Cheap enough once per game, not once per move.
[[nodiscard]] std::vector<int64_t> processTree(int64_t pid);

// `pid` and its descendants among the (process, parent) pairs of a list of every process, each once, at most
// MAX_PROCESS_TREE: process ids are reused (Windows reuses them soon), so a process may name as its parent one of its
// own descendants, a cycle that must not grow the tree without end.
inline constexpr std::size_t MAX_PROCESS_TREE = 4096;
[[nodiscard]] std::vector<int64_t> descendants(int64_t pid, const std::vector<std::pair<int64_t, int64_t>>& parents);

// Processor time (user and system) these processes used so far, in milliseconds; -1 when it cannot be read.
[[nodiscard]] int64_t cpuTimeMs(const std::vector<int64_t>& pids);

}  // namespace fcg
