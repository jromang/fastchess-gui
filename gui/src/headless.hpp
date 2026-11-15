#pragma once

// fastchess-gui --run <tournament folder | setup file>: plays a tournament without a window, printing progress.

#include <filesystem>
#include <optional>
#include <string>

#include "control_child.hpp"
#include "storage.hpp"
#include "topology.hpp"

namespace fcg {

// What to broadcast while playing: from the command line, else from the setup file's broadcast settings.
struct HeadlessBroadcast {
    std::optional<int> tlcs_port;         // TLCS server on this UDP port
    bool tlcs_lanes = false;              // and every concurrent game on a port of its own, from it on
    std::optional<int> web_port;          // web page on this TCP port
    std::optional<std::string> live_dir;  // live files in this folder; empty = "live" in the tournament folder
    std::optional<std::string> lichess;   // a lichess round's URL or id, "new" for a new broadcast
    std::optional<std::string> tunnel;    // the web page on the internet: "localhost.run" or ssh://user@host...
};

// Resumes the tournament in `target`, or creates one from a setup (or template) file with the engines of the
// profile, and plays it to the end. Ctrl+C stops it, resumable. Returns the process exit code: 0 finished,
// 1 error while playing, 2 nothing to run, 3 stopped before the end.
//
// With `control` (--control stdio), `target` is a tournament folder, and the process talks with the window through
// the control channel (control.hpp) instead of printing: the window started it to play that tournament.
int runHeadless(const std::string& target, const Storage& storage, const CpuTopology& topology,
                const HeadlessBroadcast& broadcast = {}, const ControlOptions& control = {});

// A setup (or template) file made into a new tournament folder, with the engines of the profile, as runHeadless does
// with one: the folder, or nullopt when it cannot be (the reason on the standard error). --run ... --detach plays the
// folder in the background, the control channel taking only tournament folders.
std::optional<std::filesystem::path> tournamentFromSetup(const std::string& file, const Storage& storage,
                                                         const CpuTopology& topology);

}  // namespace fcg
