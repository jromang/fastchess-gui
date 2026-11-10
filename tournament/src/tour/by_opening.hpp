#pragma once

// Results by opening: every position the games started from, with what its games and game pairs gave. It tells
// which openings are decisive, which are always drawn, and which favour one colour, to choose or clean a book.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <game/book/opening.hpp>

#include "state.hpp"

namespace fcg::tour {

struct OpeningStats {
    std::string fen;                 // the position the engines started from, after the book moves (EPD)
    std::string book_fen;            // the book's position, before its moves
    std::vector<std::string> moves;  // the book moves, UCI
    std::string eco;                 // the classification of the position (the deepest known along the book
    std::string name;                // moves), empty when it is not a known opening (Chess960, odd positions)

    int games           = 0;
    int pairs           = 0;  // complete pairs: both games, colours reversed, counted
    double white_points = 0;
    int draws           = 0;
    uint64_t plies      = 0;

    // Complete pairs that ended this way.
    int white_twice      = 0;  // White won both games: the opening favours White
    int black_twice      = 0;
    int one_engine_twice = 0;  // the same engine won both: the opening tells the engines apart
    int drawn_twice      = 0;

    std::vector<uint64_t> game_numbers;  // 1-based, as the Games tab shows them

    [[nodiscard]] double whiteScore() const noexcept { return games ? white_points / games : 0.0; }
    [[nodiscard]] double drawRate() const noexcept { return games ? static_cast<double>(draws) / games : 0.0; }
    // Pairs a colour won twice, among the complete pairs.
    [[nodiscard]] double unbalanced() const noexcept {
        return pairs ? static_cast<double>(white_twice + black_twice) / pairs : 0.0;
    }
};

// Where a book opening leads: the position after its moves (EPD), the moves (UCI), and the classification of the
// deepest known position along them (empty eco and name when none is known, and in Chess960).
struct ClassifiedOpening {
    std::string fen;
    std::vector<std::string> moves;
    std::string eco;
    std::string name;
};
[[nodiscard]] ClassifiedOpening classify(const fastchess::book::Opening& opening, bool chess960);

// One entry per starting position, in the order they were first played. Voided games do not count.
[[nodiscard]] std::vector<OpeningStats> resultsByOpening(const std::vector<GameResult>& results,
                                                         const std::map<uint32_t, fastchess::book::Opening>& openings,
                                                         bool chess960);

// The openings' starting positions, one EPD line each: a book of them.
[[nodiscard]] std::string openingsEpd(const std::vector<const OpeningStats*>& openings);

}  // namespace fcg::tour
