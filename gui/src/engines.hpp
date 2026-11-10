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
    std::string protocol;  // "uci" or "xboard": the entry's, or the one found out
    tour::FileFingerprint program;
};

// Finds out whether the engine speaks UCI or WinBoard (xboard). Blocks for up to 13 seconds.
[[nodiscard]] std::string detectProtocol(const EngineEntry& entry);

// Finds out the protocol when the entry does not say it, then starts the engine, waits for uciok and quits it. Blocks
// for up to the startup timeout, so call it from a background thread. Needs config::TournamentConfig to be set.
[[nodiscard]] ProbeResult probeEngine(const EngineEntry& entry);

struct CheckResult {
    bool ok = false;
    std::string message;  // what was measured, or what went wrong
    std::string report;   // the UCI compliance steps, one per line, with the engine's output after a failed one
};

// Starts the engine with its options, measures how long it takes to answer uciok and isready, lets it think on the
// initial position and checks that its move is legal; then runs fastchess' UCI compliance steps (not for an engine
// spoken to in xboard). Blocks for up to the startup timeout plus a few seconds.
[[nodiscard]] CheckResult checkEngine(const EngineEntry& entry);

// A WinBoard engine: its protocol is xboard, or it goes through an external adapter (wb2uci).
[[nodiscard]] bool isWinboard(const EngineEntry& entry);

// Sets entry.program_state. A program never fingerprinted (an engine list of an earlier version) is taken as it is
// now. Cheap unless the program's date moved: then its CRC is read. Returns true when the entry must be saved.
bool refreshProgram(EngineEntry& entry);

// The working directory fastchess should use for this engine.
[[nodiscard]] std::string engineWorkingDir(const EngineEntry& entry);

// Removes options the engine does not have (any more) and values equal to the default.
void pruneOptions(EngineEntry& entry);

}  // namespace fcg
