#pragma once

// The user guide (docs/guide.md) on GitHub, opened at the section of a page: the `?` of the page titles and F1.

#include <string>
#include <string_view>
#include <vector>

namespace fcg::guide {

// The sections the pages and tabs lead to.
enum class Section {
    Window,         // The window: the navigation, Log and Settings
    Engines,        // Engines
    Books,          // Books
    TestSuites,     // Test suites
    NewTournament,  // New tournament
    Running,        // Running a tournament: the home page and the workspace's tabs
    Changing,       // Changing a running tournament: the Schedule tab
    Ratings,        // Ratings
    Broadcast,      // Broadcast
    Count
};

// The anchor of a section, as GitHub makes it from its heading: "running-a-tournament".
[[nodiscard]] const char* anchor(Section section);

// The anchor GitHub gives a heading: in lower case, spaces become dashes, punctuation goes away (letters, digits,
// dashes and underscores stay; so do the bytes of letters outside ASCII).
[[nodiscard]] std::string slug(std::string_view heading);

// The anchors of every heading of a Markdown text, in order, outside fenced code blocks; a heading met again gets
// "-1", "-2"... as on GitHub.
[[nodiscard]] std::vector<std::string> anchors(std::string_view markdown);

// https://github.com/jromang/fastchess-gui/blob/gui/docs/guide.md#<anchor>
[[nodiscard]] std::string url(Section section);

// Opens url(section) in the browser.
void open(Section section);

}  // namespace fcg::guide
