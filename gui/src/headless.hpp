#pragma once

// fastchess-gui --run <tournament folder | setup file>: plays a tournament without a window, printing progress.

#include <optional>
#include <string>

#include "storage.hpp"
#include "topology.hpp"

namespace fcg {

// What to broadcast while playing: from the command line, else from the setup file's broadcast settings.
struct HeadlessBroadcast {
    std::optional<int> tlcs_port;         // TLCS server on this UDP port
    std::optional<int> web_port;          // web page on this TCP port
    std::optional<std::string> live_dir;  // live files in this folder; empty = "live" in the tournament folder
};

// Resumes the tournament in `target`, or creates one from a setup (or template) file with the engines of the
// profile, and plays it to the end. Ctrl+C stops it, resumable. Returns the process exit code: 0 finished,
// 1 error while playing, 2 nothing to run, 3 stopped before the end.
int runHeadless(const std::string& target, const Storage& storage, const CpuTopology& topology,
                const HeadlessBroadcast& broadcast = {});

}  // namespace fcg
