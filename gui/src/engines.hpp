#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "model.hpp"

namespace fcg {

// How long an engine may take to answer uciok, when probed and in games. fastchess' 10 s is too short for engines
// that load a large network before reading stdin, on a slow machine or on a first launch the antivirus scans.
inline constexpr std::chrono::milliseconds ENGINE_STARTUP_TIME = std::chrono::seconds(60);

// Parses "option name <n> type <t> [default <d>] [min <a>] [max <b>] [var <v>]...".
[[nodiscard]] std::optional<UciOptionInfo> parseUciOption(std::string_view line);

struct ProbeResult {
    bool ok = false;
    std::string error;
    std::string id_name;
    std::string id_author;
    std::vector<UciOptionInfo> options;
};

// Starts the engine, waits for uciok and quits it. Blocks for up to the startup timeout,
// so call it from a background thread. Needs config::TournamentConfig to be set.
[[nodiscard]] ProbeResult probeEngine(const EngineEntry& entry);

// The working directory fastchess should use for this engine.
[[nodiscard]] std::string engineWorkingDir(const EngineEntry& entry);

// Removes options the engine does not have (any more) and values equal to the default.
void pruneOptions(EngineEntry& entry);

}  // namespace fcg
