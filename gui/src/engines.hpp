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

// `text` as UTF-8: unchanged when it is already, otherwise read as Windows-1252 (Latin-1), which old engines and
// engine lists of Windows programs write their accents in. JSON files take only UTF-8.
[[nodiscard]] std::string toUtf8(std::string text);

// Parses "option name <n> type <t> [default <d>] [min <a>] [max <b>] [var <v>]...".
[[nodiscard]] std::optional<UciOptionInfo> parseUciOption(std::string_view line);

struct ProbeResult {
    bool ok = false;
    std::string error;
    std::string output;  // a failed read: what the engine wrote last
    std::string id_name;
    std::string id_author;
    std::vector<UciOptionInfo> options;
    std::string protocol;  // "uci" or "xboard": the entry's, or the one found out
    tour::FileFingerprint program;
    bool restart = false;  // a WinBoard engine that asks to be started again for every game (reuse=0)
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

// Starts the engine and sends its options (one it does not have or a value it refuses fails the check), measures how
// long it takes to answer uciok and isready, lets it think on the initial position and checks that its move is legal;
// then runs fastchess' UCI compliance steps, its options sent too (not for an engine spoken to in xboard). Blocks for
// up to the startup timeout plus a few seconds. Failures come with what the engine wrote last, in the report.
[[nodiscard]] CheckResult checkEngine(const EngineEntry& entry);

// The same program, with the same arguments: the same engine, whatever its options (its first read changes them).
// Windows file names ignore case: C:\Engines\SF.EXE is c:\engines\sf.exe.
[[nodiscard]] bool sameEngine(const EngineEntry& a, const EngineEntry& b);

// What the engine searches look in besides the engine's name: the name the engine reports, its author, its program
// and the notes, one per line.
[[nodiscard]] std::string engineSearchText(const EngineEntry& entry);

// The engine's name or its engineSearchText() contains `needle`, letter case aside. An empty needle takes every engine.
[[nodiscard]] bool engineMatches(const EngineEntry& entry, const std::string& needle);

// A WinBoard engine: its protocol is xboard, or it goes through an external adapter (wb2uci).
[[nodiscard]] bool isWinboard(const EngineEntry& entry);

// Sets entry.program_state. A program never fingerprinted (an engine list of an earlier version) is taken as it is
// now. Cheap unless the program's date moved: then its CRC is read. Returns true when the entry must be saved.
bool refreshProgram(EngineEntry& entry);

// refreshProgram in two parts, to read the CRCs of many programs off the UI thread. The first asks only the file
// system: it sets entry.program_state and returns false, or returns true, the state left as it was, when only the
// program's CRC can tell (a program never fingerprinted, or the same size under another date).
[[nodiscard]] bool programNeedsCrc(EngineEntry& entry);
// The second, with the program's fingerprint now (tour::fingerprint). Returns true when the entry must be saved.
bool applyFingerprint(EngineEntry& entry, const tour::FileFingerprint& now);

// The working directory fastchess should use for this engine.
[[nodiscard]] std::string engineWorkingDir(const EngineEntry& entry);

// A string option that holds a file (EvalFile, BookFile) or a folder (SyzygyPath, NalimovPath), told by its name: the
// option editor offers to browse for it.
enum class OptionPath { None, File, Folder };
[[nodiscard]] OptionPath optionPath(const UciOptionInfo& option);

// Removes options the engine does not have (any more) and values equal to the default.
void pruneOptions(EngineEntry& entry);

// A newly added engine that can report its win, draw and loss shares (UCI_ShowWDL) is asked to. Set off by hand, the
// option stays off.
void showWdlByDefault(EngineEntry& entry);

}  // namespace fcg
