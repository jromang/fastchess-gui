#pragma once

// fastchess-gui --run <tournament folder | setup file>: plays a tournament without a window, printing progress.

#include <string>

#include "storage.hpp"
#include "topology.hpp"

namespace fcg {

// Resumes the tournament in `target`, or creates one from a setup (or template) file with the engines of the
// profile, and plays it to the end. Ctrl+C stops it, resumable. Returns the process exit code: 0 finished,
// 1 error while playing, 2 nothing to run, 3 stopped before the end.
int runHeadless(const std::string& target, const Storage& storage, const CpuTopology& topology);

}  // namespace fcg
