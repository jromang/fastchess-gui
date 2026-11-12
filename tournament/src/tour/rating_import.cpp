#include "rating_import.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>

#include "game_db.hpp"

namespace fcg::tour {

namespace {

using Cells = std::vector<std::string>;

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

std::string trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return std::string(s);
}

std::string lower(std::string s) {
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool hasLetter(std::string_view s) {
    for (const char c : s) {
        if (isLetter(c)) return true;
    }
    return false;
}

// "3650", "3610.2", "3610,2": a number with no sign, nullopt when the text is not one.
std::optional<double> number(std::string_view text) {
    const auto s       = trim(text);
    std::size_t digits = 0, points = 0;
    for (const char c : s) {
        if (isDigit(c))
            ++digits;
        else if (c == '.' || c == ',')
            ++points;
        else
            return std::nullopt;
    }
    if (digits == 0 || points > 1 || !isDigit(s.front()) || !isDigit(s.back())) return std::nullopt;
    std::string plain = s;
    for (auto& c : plain) {
        if (c == ',') c = '.';
    }
    return std::strtod(plain.c_str(), nullptr);
}

// A rating as the reference Elo takes it, nullopt when it is out of its range.
std::optional<int> rating(std::string_view text) {
    const auto value = number(text);
    if (!value) return std::nullopt;
    const auto elo = std::lround(*value);
    if (elo < 1 || elo > 5000) return std::nullopt;
    return static_cast<int>(elo);
}

// A rank before a name: "12", "4-5", "3.", "3)".
bool isRank(std::string_view token) {
    if (token.empty() || !isDigit(token.front())) return false;
    if (token.back() == '.' || token.back() == ')') token.remove_suffix(1);
    bool dash = false;
    for (std::size_t i = 0; i < token.size(); ++i) {
        if (isDigit(token[i])) continue;
        if (token[i] != '-' || dash || i + 1 == token.size()) return false;
        dash = true;
    }
    return true;
}

// The name with the rank before it dropped, when something follows.
std::string withoutRank(const std::string& field) {
    const auto space = field.find(' ');
    if (space == std::string::npos || !isRank(std::string_view(field).substr(0, space))) return field;
    return trim(std::string_view(field).substr(space));
}

void appendUtf8(std::string& out, uint32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x110000) {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// The character an HTML entity ("amp", "#8722", "#x2212") stands for; a no-break space is a space.
std::optional<uint32_t> entity(std::string_view name) {
    if (name.size() > 1 && name[0] == '#') {
        const bool hex   = name[1] == 'x' || name[1] == 'X';
        const auto start = hex ? 2u : 1u;
        if (name.size() <= start) return std::nullopt;
        uint32_t code = 0;
        for (const char c : name.substr(start)) {
            const int digit = isDigit(c)                    ? c - '0'
                              : hex && c >= 'a' && c <= 'f' ? c - 'a' + 10
                              : hex && c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                            : -1;
            if (digit < 0 || code > 0x10FFFF) return std::nullopt;
            code = code * (hex ? 16 : 10) + static_cast<uint32_t>(digit);
        }
        return code == 0xA0 ? ' ' : code;
    }
    static const std::map<std::string_view, uint32_t> names = {
        {"nbsp", ' '},  {"amp", '&'},      {"lt", '<'},       {"gt", '>'},       {"quot", '"'},
        {"apos", '\''}, {"minus", 0x2212}, {"ndash", 0x2013}, {"mdash", 0x2014}, {"plusmn", 0xB1}};
    const auto it = names.find(name);
    if (it == names.end()) return std::nullopt;
    return it->second;
}

// An HTML page as text: the cells of a table row separated by tabs, a row or a block on a line of its own, the
// entities decoded. The spaces of the source are one; those of entities (a text list aligned with &nbsp;) stay.
std::string htmlToText(std::string_view html) {
    std::string out;
    bool in_row        = false;  // between <tr> and </tr>: a block or a line break is a space, not a new line
    bool space         = false;  // whitespace of the source met, written as one space before what comes next
    const auto lowered = lower(std::string(html));
    for (std::size_t i = 0; i < html.size();) {
        const char c = html[i];
        if (c == '<') {
            // Comments, scripts and styles are dropped whole.
            bool skipped = false;
            for (const std::string_view skip : {"<!--", "<script", "<style"}) {
                if (lowered.compare(i, skip.size(), skip) != 0) continue;
                const std::string_view end = skip == "<!--" ? "-->" : skip == "<script" ? "</script>" : "</style>";
                const auto stop            = lowered.find(end, i);
                i                          = stop == std::string::npos ? html.size() : stop + end.size();
                skipped                    = true;
                break;
            }
            if (skipped) continue;
            const auto end = html.find('>', i);
            if (end == std::string::npos) break;
            std::string tag;
            std::size_t k      = i + 1;
            const bool closing = k < html.size() && html[k] == '/';
            if (closing) ++k;
            while (k < end && isLetter(html[k])) tag += lowered[k++];
            if (k < end && isDigit(html[k]) && tag == "h") tag += html[k];
            i = end + 1;

            if (tag == "tr") {
                out += '\n';
                in_row = !closing;
                space  = false;
            } else if (tag == "table") {
                out += '\n';
                in_row = false;
                space  = false;
            } else if ((tag == "td" || tag == "th") && !closing) {
                out += '\t';
                space = false;
            } else if (tag == "br" || tag == "p" || tag == "div" || tag == "li" || tag == "pre" ||
                       (tag.size() == 2 && tag[0] == 'h' && isDigit(tag[1]))) {
                if (in_row) {
                    space = true;
                } else {
                    out += '\n';
                    space = false;
                }
            }
            continue;
        }
        if (isSpace(c)) {
            space = true;
            ++i;
            continue;
        }
        if (space && !out.empty() && out.back() != '\n' && out.back() != '\t') out += ' ';
        space = false;
        if (c == '&') {
            const auto end = html.find(';', i);
            if (end != std::string::npos && end - i <= 10) {
                if (const auto code = entity(html.substr(i + 1, end - i - 1))) {
                    appendUtf8(out, *code);
                    i = end + 1;
                    continue;
                }
            }
        }
        out += c;
        ++i;
    }
    return out;
}

Cells split(std::string_view line, char separator) {
    Cells cells;
    std::string cell;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (separator != '\t' && c == '"') {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"') {
                cell += '"';
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (c == separator && !quoted) {
            cells.push_back(trim(cell));
            cell.clear();
        } else {
            cell += c;
        }
    }
    cells.push_back(trim(cell));
    return cells;
}

// Columns aligned with spaces: fields apart by two spaces or more.
Cells fields(std::string_view line) {
    Cells out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && isSpace(line[i])) ++i;
        if (i == line.size()) break;
        std::size_t end = i;
        while (end < line.size() && !(isSpace(line[end]) && (end + 1 == line.size() || isSpace(line[end + 1])))) ++end;
        out.push_back(trim(line.substr(i, end - i)));
        i = end;
    }
    return out;
}

struct Columns {
    std::size_t name = 0, elo = 0;
};

std::optional<ListedRating> made(std::string name, std::optional<int> elo) {
    if (!elo || name.empty() || !hasLetter(name)) return std::nullopt;
    return ListedRating{std::move(name), *elo};
}

// The name is the first cell with a letter, the rating the first number after it.
std::optional<ListedRating> firstNameThenNumber(const Cells& cells) {
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (!hasLetter(cells[i])) continue;
        for (std::size_t j = i + 1; j < cells.size(); ++j) {
            if (number(cells[j])) return made(cells[i], rating(cells[j]));
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// Cells of a table, a CSV line or a line copied from a web page. A header row tells where the name and the rating are
// in the rows after it.
std::optional<ListedRating> fromCells(const Cells& cells, std::optional<Columns>& columns) {
    std::optional<std::size_t> name, elo;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const auto head = lower(cells[i]);
        if (!name && (head == "name" || head == "engine" || head == "player" || head == "program")) name = i;
        if (!elo && (head == "elo" || head == "rating")) elo = i;
    }
    if (name && elo) {
        columns = Columns{*name, *elo};
        return std::nullopt;
    }
    if (columns && std::max(columns->name, columns->elo) < cells.size()) {
        if (auto r = made(withoutRank(cells[columns->name]), rating(cells[columns->elo]))) return r;
    }
    return firstNameThenNumber(cells);
}

// Ordo's "   1 Stockfish 13          :  4098.1   50.7 ...": rank, name and a mark ("<", ">", "*") before the colon.
std::optional<ListedRating> fromOrdo(std::string_view line) {
    const auto colon = line.find(" :");
    if (colon == std::string_view::npos) return std::nullopt;
    const auto after = fields(line.substr(colon + 2));
    if (after.empty()) return std::nullopt;
    const auto first = after[0].substr(0, after[0].find(' '));
    if (!number(first)) return std::nullopt;
    auto name = trim(line.substr(0, colon));
    if (name.size() > 2 && (name.back() == '<' || name.back() == '>' || name.back() == '*') &&
        name[name.size() - 2] == ' ') {
        name = trim(std::string_view(name).substr(0, name.size() - 1));
    }
    return made(withoutRank(name), rating(first));
}

// CCRL's text lists: "    1-2 Stockfish 19 64-bit 4CPU                3650  +16  -16  53.4% ...".
std::optional<ListedRating> fromColumns(std::string_view line) {
    auto cells = fields(line);
    if (cells.size() >= 2 && isRank(cells[0])) cells.erase(cells.begin());
    if (cells.empty()) return std::nullopt;
    cells[0] = withoutRank(cells[0]);
    return firstNameThenNumber(cells);
}

}  // namespace

std::vector<ListedRating> parseRatingList(std::string_view input) {
    std::string text(input);
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) text.erase(0, 3);
    const auto lowered = lower(text.substr(0, std::min<std::size_t>(text.size(), 1 << 20)));
    if (lowered.find("<td") != std::string::npos || lowered.find("<br") != std::string::npos ||
        lowered.find("<html") != std::string::npos) {
        text = htmlToText(text);
    }
    // No-break spaces (a web page copied) are spaces.
    {
        std::string spaced;
        spaced.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i) {
            const bool nbsp = text[i] == '\xC2' && i + 1 < text.size() && text[i + 1] == '\xA0';
            spaced += nbsp ? ' ' : text[i];
            i += nbsp ? 1 : 0;
        }
        text = std::move(spaced);
    }

    // Each line is read by its look; a list has one look, that of most of its engines: the other lines read as a
    // rating ("Computed on November 7, 2026") are not engines.
    enum Look { Tabs, Ordo, Csv, Aligned, LOOKS };
    std::vector<std::pair<Look, ListedRating>> found;
    std::array<std::size_t, LOOKS> count{};
    std::optional<Columns> columns;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        auto end = text.find('\n', begin);
        if (end == std::string::npos) end = text.size();
        std::string_view line(text.data() + begin, end - begin);
        begin = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (trim(line).empty()) continue;

        std::optional<ListedRating> r;
        Look look = Aligned;
        if (line.find('\t') != std::string_view::npos) {
            look = Tabs;
            r    = fromCells(split(line, '\t'), columns);
        } else if (auto ordo = fromOrdo(line)) {
            look = Ordo;
            r    = std::move(ordo);
        } else if (line.find(',') != std::string_view::npos || line.find(';') != std::string_view::npos) {
            // Commas, or semicolons where the decimal mark is a comma.
            look = Csv;
            r    = fromCells(split(line, line.find(';') != std::string_view::npos ? ';' : ','), columns);
        } else {
            r = fromColumns(line);
        }
        if (!r) continue;
        count[look]++;
        found.emplace_back(look, std::move(*r));
    }

    const auto look = static_cast<Look>(std::max_element(count.begin(), count.end()) - count.begin());
    std::vector<ListedRating> out;
    std::set<std::string> seen;
    for (auto& [l, r] : found) {
        if (l == look && seen.insert(r.name).second) out.push_back(std::move(r));
    }
    return out;
}

RatingMatches matchRatings(const std::vector<ListedRating>& list, const std::vector<std::string>& library) {
    std::vector<std::string> names;
    names.reserve(list.size());
    for (const auto& r : list) names.push_back(r.name);
    const auto engines = libraryEngines(names, library);

    RatingMatches out;
    std::map<std::string, std::size_t> index;  // library engine -> its place in out.matched
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (!engines[i]) {
            out.unmatched.push_back(list[i].name);
            continue;
        }
        const auto& engine     = *engines[i];
        const auto [it, added] = index.emplace(engine, out.matched.size());
        RatingMatch match{engine, list[i].name, list[i].elo};
        if (added)
            out.matched.push_back(std::move(match));
        else if (list[i].name == engine)
            out.matched[it->second] = std::move(match);
    }
    return out;
}

}  // namespace fcg::tour
