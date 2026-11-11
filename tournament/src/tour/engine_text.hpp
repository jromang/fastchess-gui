#pragma once

// Text for the engines. The program speaks UTF-8 (its Windows manifest says so), but a Windows engine reads the
// bytes it gets in the system's ANSI code page: a book in C:\Échecs given as UTF-8 is a file it cannot find.

#include <string>
#include <utility>
#include <vector>

namespace fcg::tour {

// On Windows, `utf8` in the system's ANSI code page when every character has a place in it; otherwise, for an
// existing file or folder, its short 8.3 name if the drive keeps them; otherwise `utf8` unchanged. Elsewhere, and
// for plain ASCII, `utf8` itself.
[[nodiscard]] std::string forEngine(const std::string& utf8);

// The values of UCI options, through forEngine().
[[nodiscard]] std::vector<std::pair<std::string, std::string>> forEngine(
    std::vector<std::pair<std::string, std::string>> options);

}  // namespace fcg::tour
