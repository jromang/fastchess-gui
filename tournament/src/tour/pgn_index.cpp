#include "pgn_index.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace fcg::tour {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// A tag line, `[Name "value"]`: its name and value (unescaped), false for anything else (a comment's [%eval 0.3]).
bool tagOf(std::string_view line, std::string_view& name, std::string& value) {
    if (line.size() < 5 || line.front() != '[' || line.back() != ']') return false;
    std::size_t i = 1;
    while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) ++i;
    if (i == 1) return false;
    name = line.substr(1, i - 1);
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= line.size() || line[i] != '"') return false;
    value.clear();
    for (++i; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) {
            value += line[++i];
        } else if (line[i] == '"') {
            return true;
        } else {
            value += line[i];
        }
    }
    return false;
}

}  // namespace

PgnIndex indexPgn(std::istream& in, const std::atomic<bool>* cancel, std::atomic<uint64_t>* done) {
    PgnIndex index;
    std::unordered_map<std::string, uint32_t> ids{{"", 0}};
    const auto intern = [&](const std::string& s) -> uint32_t {
        const auto [it, added] = ids.emplace(s, static_cast<uint32_t>(index.strings.size()));
        if (added) index.strings.push_back(s);
        return it->second;
    };

    PgnIndex::Game game;
    bool open      = false;  // a game is being read
    bool in_header = false;
    uint64_t at    = 0;  // where the line read starts
    std::string line, value;
    std::string_view name;
    const auto close = [&](uint64_t end) {
        if (!open) return;
        game.span.length = static_cast<uint32_t>(std::min<uint64_t>(end - game.span.offset, UINT32_MAX));
        index.games.push_back(game);
        game = {};
        open = false;
    };

    while (std::getline(in, line)) {
        const uint64_t start = at;
        at += line.size() + (in.eof() ? 0 : 1);
        if ((index.games.size() & 1023) == 0 || at - start > 4096) {
            if (done) *done = at;
            if (cancel && *cancel) break;
        }

        std::string_view text(line);
        while (!text.empty() && (text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
            text.remove_suffix(1);
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
        if (!text.empty() && text.front() == '%') continue;  // an escape line, which the standard ignores
        if (text.empty()) {
            in_header = false;
            continue;
        }
        if (tagOf(text, name, value)) {
            if (!in_header) {
                close(start);
                open             = true;
                game.span.offset = start;
            }
            in_header = true;
            if (name == "White")
                game.white = intern(value);
            else if (name == "Black")
                game.black = intern(value);
            else if (name == "Event")
                game.event = intern(value);
            else if (name == "Site")
                game.site = intern(value);
            else if (name == "Date")
                game.date = intern(value);
            else if (name == "Round")
                game.round = intern(value);
            else if (name == "ECO")
                game.eco = intern(value);
            else if (name == "Opening")
                game.opening = intern(value);
            else if (name == "Result")
                game.result = intern(value);
            else if (name == "FEN")
                game.fen = intern(value);
            else if (name == "PlyCount") {
                unsigned plies = 0;
                std::from_chars(value.data(), value.data() + value.size(), plies);
                game.plies = static_cast<uint16_t>(std::min(plies, 65535u));
            }
            continue;
        }
        // Moves (or anything else): those of the game open, or a game without tags.
        in_header = false;
        if (!open) {
            open             = true;
            game.span.offset = start;
        }
    }
    close(at);
    index.bytes = at;
    index.folded.reserve(index.strings.size());
    for (const auto& s : index.strings) index.folded.push_back(lower(s));
    if (done) *done = at;
    return index;
}

bool pgnGameMatches(const PgnIndex& index, const PgnIndex::Game& game, std::string_view filter) {
    std::size_t i = 0;
    while (i < filter.size()) {
        while (i < filter.size() && filter[i] == ' ') ++i;
        const auto from = i;
        while (i < filter.size() && filter[i] != ' ') ++i;
        if (i == from) break;
        const auto word = lower(filter.substr(from, i - from));
        bool found      = false;
        for (const uint32_t s : {game.white, game.black, game.event, game.site, game.date, game.round, game.eco,
                                 game.opening, game.result}) {
            if (s == 0) continue;
            const bool hit = s < index.folded.size() ? index.folded[s].find(word) != std::string::npos
                                                     : lower(index.str(s)).find(word) != std::string::npos;
            if (hit) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

}  // namespace fcg::tour
