#pragma once

// The games of any PGN file (or text), found from their headers alone: where each one is and its main tags, to list
// them, filter them by their tags and read one at a time. Fast and small enough for the files of millions of games
// the rating lists publish: the tags' values are kept once each.

#include <atomic>
#include <cstdint>
#include <istream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "pgn_file.hpp"

namespace fcg::tour {

struct PgnIndex {
    struct Game {
        PgnSpan span;  // from its first tag to the next game's
        // Indexes into `strings`; 0 is the empty string.
        uint32_t white = 0, black = 0, event = 0, site = 0, date = 0, round = 0, eco = 0, opening = 0;
        uint32_t result = 0;
        uint32_t fen    = 0;  // its FEN tag: a game from another position
        uint16_t plies  = 0;  // its PlyCount tag, 0 when it has none
    };
    std::vector<Game> games;
    std::vector<std::string> strings{""};
    std::vector<std::string> folded;  // `strings` in lower case, for the filter
    uint64_t bytes = 0;               // read

    [[nodiscard]] const std::string& str(uint32_t i) const { return i < strings.size() ? strings[i] : strings[0]; }
    [[nodiscard]] std::size_t size() const noexcept { return games.size(); }
};

// Indexes the games of `in`. A game starts at a tag line ([Tag "value"]) that follows something else than a tag (its
// moves, a blank line, the start); text before the first tag, and moves without tags, make a game of their own.
// `cancel`, once set, stops it with what it found so far; `done` follows the bytes read.
[[nodiscard]] PgnIndex indexPgn(std::istream& in, const std::atomic<bool>* cancel = nullptr,
                                std::atomic<uint64_t>* done = nullptr);

// Whether a game matches what was typed in the filter: every word of it found in its players, event, site, date,
// round, opening, ECO code or result, letter case aside. An empty filter matches every game.
[[nodiscard]] bool pgnGameMatches(const PgnIndex& index, const PgnIndex::Game& game, std::string_view filter);

}  // namespace fcg::tour
