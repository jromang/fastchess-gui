#pragma once

// What may be given to chess.hpp's readers from a file or a program of someone else's: they take some text they
// cannot read, and then work on squares off the board, which reads or writes outside their tables (found by
// fuzzing). Checked here first, until they refuse such text themselves.

#include <string_view>

#include <chess.hpp>

namespace fcg::tour {

// Whether the board reader may be given `fen`: a placement of eight ranks of eight squares, in digits and pieces, a
// king of each colour, castling rights in letters it knows, move counters of a few digits. It takes others, and then
// computes squares off the board (a placement with too many squares, a missing king, a castling right in another
// byte) or overflows the move number.
[[nodiscard]] bool readableFen(std::string_view fen);

// A position the move generator may be given: one king of each colour, no pawn on the first or last rank, the side
// not to move not in check, the side to move in check by two pieces at most. Any other a FEN can describe makes it
// read outside its tables, or give moves that leave a king to be taken.
[[nodiscard]] bool legalPosition(const chess::Board& board);

// Whether the SAN reader may be given `san`: one that starts as castling does (O or 0) must be castling, "O-O" or
// "O-O-O" (or with zeros), annotations after it; the reader takes any other for a king's move to no square.
[[nodiscard]] bool readableSan(std::string_view san);

}  // namespace fcg::tour
