#pragma once

// What system a program is built for, read from its first bytes (a Windows PE, a Linux ELF, a macOS Mach-O, universal
// or not, or a script), and whether a machine can run it: before an engine's program is copied to a remote machine,
// so that a Windows build is not sent to a Linux box. And the file names Windows cannot hold, for the folders copied
// to a Windows machine.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fcg {

struct RemoteSystem;

struct ProgramFormat {
    enum class Kind {
        Unknown,  // none of those below: copied all the same, with a warning
        Windows,  // PE ("MZ")
        Elf,      // Linux or another Unix (`os` says which)
        MachO,    // macOS, universal or not
        Script,   // "#!": run by the interpreter it names, on a POSIX system only
    };
    Kind kind = Kind::Unknown;
    std::string os;                  // as the machines' test names it: "Windows", "Linux", "FreeBSD", "Darwin"
    std::vector<std::string> archs;  // "x86_64", "arm64", "x86", "arm"...: empty when not told; several when universal
    std::string shebang;             // a script's first line, without "#!"
    std::string interpreter;         // what must be on the machine: "python3" (#!/usr/bin/env python3), "/bin/sh"

    bool operator==(const ProgramFormat&) const = default;
};

// The format of a program from its first bytes (4 KiB are enough, unless a PE header lies further: its architecture
// is then not told).
[[nodiscard]] ProgramFormat programFormat(std::string_view head);
// The same of a file; Unknown when it cannot be read.
[[nodiscard]] ProgramFormat readProgramFormat(const std::filesystem::path& file);

// "a Windows program", "a macOS program (universal: x86_64, arm64)", "a script for python3": for the messages.
[[nodiscard]] std::string describeFormat(const ProgramFormat& format);
// "Linux x86_64", "macOS arm64", "Windows x86_64": a machine's system, for the messages.
[[nodiscard]] std::string describeSystem(const RemoteSystem& system);
// "Windows", "macOS", "Linux": the system's name alone.
[[nodiscard]] std::string systemName(const std::string& os);

// Whether a program in `format` runs on a machine of `system`: it fits, it may run (a warning says on what condition:
// Rosetta, emulation, 32-bit libraries, a format not known), or it cannot (refused, the message saying which build
// to choose). `engine` and `machine` are names for the messages.
struct Suitability {
    enum class Level { Fits, Warning, Refused };
    Level level = Level::Fits;
    std::string message;  // empty when it fits
};
[[nodiscard]] Suitability programSuits(const ProgramFormat& format, const RemoteSystem& system,
                                       const std::string& engine, const std::string& machine);

// Why Windows cannot hold a file or folder name ("aux.txt", "a:b", "notes."), empty when it can.
[[nodiscard]] std::string windowsNameProblem(std::string_view name);
// A name for an engine's folder on any system: the characters no system takes as a name replaced by '_', no trailing
// dot or space, a reserved Windows name made another one.
[[nodiscard]] std::string safeFolderName(std::string_view name);

}  // namespace fcg
