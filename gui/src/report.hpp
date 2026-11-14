#pragma once

// "Report a problem…": a new GitHub issue opened in the browser, from the bug template
// (.github/ISSUE_TEMPLATE/bug_report.yml), its fields filled with what a maintainer needs: the version, the system,
// the graphics driver and the last lines of the log. The user reads and edits it before sending it; the program
// itself sends nothing.

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fcg::report {

inline constexpr const char* NEW_ISSUE = "https://github.com/jromang/fastchess-gui/issues/new";

// What goes in the issue. Each field fills the template's field of the same id.
struct Facts {
    std::string version;           // "fastchess-gui 0.5.9"
    std::string system;            // the system, processor, memory, graphics driver
    std::vector<std::string> log;  // the last lines of the log, oldest first
    std::string seen;              // what went wrong, when the program knows (a startup failure); empty otherwise
};

// The address of the new issue: NEW_ISSUE?template=bug_report.yml&version=...&system=...&log=..., at most `limit`
// characters (servers refuse longer addresses): the oldest lines of the log are left out first, then the rest is cut.
[[nodiscard]] std::string issueUrl(const Facts& facts, std::size_t limit = 7000);

// Every byte but letters, digits and -._~ as %XX (UTF-8 kept as its bytes).
[[nodiscard]] std::string percentEncode(std::string_view text);

// The last `count` lines of a text file, oldest first: of the log, fastchess-gui.log in the profile. Only its end is
// read, however long the session was.
[[nodiscard]] std::vector<std::string> lastLines(const std::filesystem::path& file, std::size_t count);

// The system this runs on, as the system names it: "Ubuntu 24.04.1 LTS, Linux 6.8.0 (x86_64)", "Windows 10.0
// build 22631 (x86_64)", "macOS 15.1 (arm64)".
[[nodiscard]] std::string systemName();

}  // namespace fcg::report
