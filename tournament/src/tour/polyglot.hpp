#pragma once

// Polyglot opening books (.bin), which fastchess does not read: their lines are followed from the initial position
// and the positions they lead to become an EPD book. Or a participant plays from one, as its own book.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <chess.hpp>

namespace fcg::tour {

[[nodiscard]] bool isPolyglot(const std::filesystem::path& file);

// Plays `move` on a board whose positions are looked up in a Polyglot book. chess-library's numbers are those of the
// format, so the board's hash() is the position's key, but for en passant: the format hashes the file whenever a pawn
// of the side to move stands beside the pawn that just stepped two squares, which makeMove<false> does, whereas
// makeMove<true> hashes it only when the capture is legal (not when the capturing pawn is pinned).
inline void playForBook(chess::Board& board, chess::Move move) { board.makeMove<false>(move); }

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

// A Polyglot book looked up position by position: a participant's own book (see book_adapter.hpp). The entries
// are read from the file at each lookup, by a binary search on their keys (the format keeps them sorted), so that
// a large book costs no memory in the adapter of each engine.
class PolyglotBook {
   public:
    struct Choice {
        chess::Move move;  // castling as the king taking its rook, as chess-library and the format write it
        uint16_t weight = 0;
    };

    // Throws std::runtime_error with a readable message when the file cannot be read or is no Polyglot book.
    explicit PolyglotBook(const std::filesystem::path& file);

    // The legal moves the book gives `board`, in the book's order, those that weigh nothing left out. The moves that
    // led to `board` are played with playForBook(). (A position set from a FEN keeps its en passant square only when
    // the capture is legal: when it is not, the key differs from the format's, which rarely matters.)
    [[nodiscard]] std::vector<Choice> moves(const chess::Board& board);

    // One of them, at random in proportion to their weights, as Polyglot and Cute Chess choose; NO_MOVE when the
    // position is not in the book.
    [[nodiscard]] chess::Move pick(const chess::Board& board, std::mt19937_64& rng);

   private:
    std::ifstream in_;
    uint64_t count_ = 0;  // entries
};

// Whether a participant's own book still plays at `ply` (0: the first move of the game, counted from the position
// the game started from, the moves of the tournament's opening included); `depth` 0 is the whole book.
[[nodiscard]] inline bool withinBookDepth(std::size_t ply, int depth) noexcept {
    return depth <= 0 || ply < static_cast<std::size_t>(depth);
}

}  // namespace fcg::tour
