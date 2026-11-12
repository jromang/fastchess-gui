#include "guide.hpp"

#include <map>

#include <SDL3/SDL.h>

namespace fcg::guide {

const char* anchor(Section section) {
    switch (section) {
        case Section::Window:
            return "the-window";
        case Section::Engines:
            return "engines";
        case Section::Books:
            return "books";
        case Section::TestSuites:
            return "test-suites";
        case Section::NewTournament:
            return "new-tournament";
        case Section::Running:
            return "running-a-tournament";
        case Section::Changing:
            return "changing-a-running-tournament";
        case Section::Ratings:
            return "ratings";
        case Section::Broadcast:
            return "broadcast";
        case Section::Count:
            break;
    }
    return "";
}

std::string slug(std::string_view heading) {
    std::string out;
    for (const char c : heading) {
        const auto u = static_cast<unsigned char>(c);
        if (c >= 'A' && c <= 'Z')
            out += static_cast<char>(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || u >= 0x80)
            out += c;
        else if (c == ' ')
            out += '-';
    }
    return out;
}

std::vector<std::string> anchors(std::string_view markdown) {
    std::vector<std::string> out;
    std::map<std::string, int> seen;
    bool fenced = false;
    while (!markdown.empty()) {
        const auto end = markdown.find('\n');
        auto line      = markdown.substr(0, end);
        markdown       = end == std::string_view::npos ? std::string_view{} : markdown.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        if (line.substr(0, 3) == "```" || line.substr(0, 3) == "~~~") {
            fenced = !fenced;
            continue;
        }
        if (fenced || line.empty() || line[0] != '#') continue;
        const auto hashes = line.find_first_not_of('#');
        if (hashes == std::string_view::npos || hashes > 6 || line[hashes] != ' ') continue;
        auto text = line.substr(hashes + 1);
        // "## Title ##": the closing hashes, after a space, are not part of it.
        while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
        if (const auto last = text.find_last_not_of('#'); last != std::string_view::npos && text[last] == ' ')
            text = text.substr(0, last);
        while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);

        auto anchor = slug(text);
        if (const int n = seen[anchor]++; n > 0) anchor += "-" + std::to_string(n);
        out.push_back(anchor);
    }
    return out;
}

std::string url(Section section) {
    return std::string("https://github.com/jromang/fastchess-gui/blob/gui/docs/guide.md#") + anchor(section);
}

void open(Section section) { SDL_OpenURL(url(section).c_str()); }

}  // namespace fcg::guide
