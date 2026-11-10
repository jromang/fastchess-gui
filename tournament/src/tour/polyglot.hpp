#pragma once

// Polyglot opening books (.bin), which fastchess does not read: their lines are followed from the initial position
// and the positions they lead to become an EPD book.

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace fcg::tour {

[[nodiscard]] bool isPolyglot(const std::filesystem::path& file);

struct PolyglotWalk {
    int plies = 8;  // how deep the lines are followed
    // Moves weighing less than this share of the best move of their position are left out, as rarely played.
    double min_share          = 0.1;
    std::size_t max_positions = 200'000;
};

// The positions (EPD: placement, side, castling, en passant) at the end of the book's lines, cut at `plies`, each
// once, in the order of the book. Throws std::runtime_error with a readable message (unreadable file, an empty
// book, too many positions).
[[nodiscard]] std::vector<std::string> polyglotPositions(const std::filesystem::path& file,
                                                         const PolyglotWalk& walk = {});

// The same, written to `epd` (one line each). Returns the number of positions.
std::size_t polyglotToEpd(const std::filesystem::path& file, const std::filesystem::path& epd,
                          const PolyglotWalk& walk = {});

}  // namespace fcg::tour
