#pragma once

// Chess324 (Larry Kaufman's): the kings and the rooks stay on their usual squares, so that castling is the usual one,
// and the queen, the bishops and the knights of each side are shuffled on the other squares of its back rank (the b,
// c, d, f and g files), its bishops on squares of opposite colours. That makes 18 arrangements of a side, White's and
// Black's chosen independently: 324 starting positions. They are ordinary positions, which an EPD book holds and
// any engine plays without Chess960.

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace fcg::tour {

// The 18 arrangements of a back rank, from the a file to the h file in White's letters ("RNBQKBNR"), the usual one
// first.
[[nodiscard]] std::vector<std::string> chess324Ranks();

// The starting positions as EPD lines (placement, side to move, castling, en passant), White to move with the usual
// castling rights: every pair of arrangements, White's in the order of chess324Ranks() and Black's within it; with
// `symmetrical`, only the 18 where Black's arrangement mirrors White's.
[[nodiscard]] std::vector<std::string> chess324Positions(bool symmetrical);

// Writes them to `file`, one per line, and returns how many. Throws std::runtime_error with the reason.
std::size_t writeChess324(const std::filesystem::path& file, bool symmetrical);

}  // namespace fcg::tour
