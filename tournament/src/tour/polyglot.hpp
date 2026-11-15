#pragma once

// Polyglot opening books (.bin), which fastchess does not read: their lines are followed from the initial position
// and the positions they lead to become an EPD book. Or a participant plays from one, as its own book.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <stdexcept>
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

// How a Polyglot book's moves are chosen: from -RANDOMNESS_LIMIT, the best move only (those of the largest weight),
// through 0, in proportion to their weights (as Polyglot and Cute Chess choose), to RANDOMNESS_LIMIT, every move
// alike. In between, each weight w of a position becomes (w / best)^e: e = 1 - r / 100 above 0 (50 takes their square
// roots), e = 100 / (100 + r) below (-50 squares them).
inline constexpr int RANDOMNESS_LIMIT = 100;

// The weights the moves of a position are chosen by with `randomness` (clamped), in the order of `weights`: the
// weights themselves at 0, 1 for each at RANDOMNESS_LIMIT, 1 for the best and 0 for the others at -RANDOMNESS_LIMIT,
// otherwise out of CHOICE_SCALE for the best. A move that weighs nothing stays at 0. Integers, so that the same seed
// draws the same moves everywhere.
inline constexpr uint64_t CHOICE_SCALE = uint64_t{1} << 32;
[[nodiscard]] std::vector<uint64_t> choiceWeights(const std::vector<uint16_t>& weights, int randomness);

// The index of the move `draw` (a number of the generator, any) chooses among `weights` (choiceWeights()): each in
// proportion to its weight. nullopt when they all weigh nothing.
[[nodiscard]] std::optional<std::size_t> drawChoice(const std::vector<uint64_t>& weights, uint64_t draw);

// The randomness in a few English words, for the line under the standings: "moves by weight", "best moves only",
// "every move alike", "randomness -40".
[[nodiscard]] std::string randomnessLabel(int randomness);

struct PolyglotWalk {
    int plies = 8;  // how deep the lines are followed
    // Moves whose weight, as `randomness` makes it (choiceWeights()), is less than this share of the best move's of
    // their position are left out, as rarely played.
    double min_share          = 0.1;
    std::size_t max_positions = 200'000;
    int randomness            = 0;
};

// A line of the book, as the walk ends it: the position it leads to (EPD: placement, side, castling, en passant) and
// the chance of drawing it move by move, each move in proportion to its weight (choiceWeights()) among those kept.
struct PolyglotLine {
    std::string epd;
    double share = 0;
};

// The book's lines, cut at `plies`, each position once (a transposition counts the first line that reaches it), in
// the order of the book. Throws std::runtime_error with a readable message (unreadable file, an empty book, too many
// positions).
[[nodiscard]] std::vector<PolyglotLine> polyglotLines(const std::filesystem::path& file, const PolyglotWalk& walk = {});

// Their positions only.
[[nodiscard]] std::vector<std::string> polyglotPositions(const std::filesystem::path& file,
                                                         const PolyglotWalk& walk = {});

// The positions of `lines` in a random order from `seed` where a line comes the sooner the likelier it is: drawn one
// after the other, each among those left in proportion to its share (Efraimidis and Spirakis' weighted sampling
// without replacement). Every position once; the same seed gives the same order.
[[nodiscard]] std::vector<std::string> drawnInOrder(const std::vector<PolyglotLine>& lines, uint64_t seed);

// The positions written to `epd` (one line each): in the order of the book, or drawn from `seed` (drawnInOrder()).
// Returns the number of positions.
std::size_t polyglotToEpd(const std::filesystem::path& file, const std::filesystem::path& epd,
                          const PolyglotWalk& walk = {}, std::optional<uint64_t> seed = std::nullopt);

// A Polyglot book whose entries are not sorted by key, as the format wants, and too large to be sorted in memory.
struct UnsortedPolyglotBook : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// A Polyglot book looked up position by position: a participant's own book (see book_adapter.hpp). The entries
// are read from the file at each lookup, by a binary search on their keys (the format keeps them sorted), so that
// a large book costs no memory in the adapter of each engine. A book that is not sorted (some tools write them so,
// and a binary search would find nothing in it) is sorted in memory when it is small enough; a larger one is
// refused.
class PolyglotBook {
   public:
    // The largest unsorted book sorted in memory, in entries (16 bytes each): 16 MB. A book up to this size is
    // checked whole; a larger one at evenly spread entries.
    static constexpr uint64_t SORT_MAX = uint64_t{1} << 20;

    struct Choice {
        chess::Move move;  // castling as the king taking its rook, as chess-library and the format write it
        uint16_t weight = 0;
    };

    // Throws std::runtime_error with a readable message when the file cannot be read or is no Polyglot book, and
    // UnsortedPolyglotBook when it is not sorted and too large to be sorted in memory.
    explicit PolyglotBook(const std::filesystem::path& file);

    // The legal moves the book gives `board`, in the book's order, those that weigh nothing left out. The moves that
    // led to `board` are played with playForBook(). (A position set from a FEN keeps its en passant square only when
    // the capture is legal: when it is not, the key differs from the format's, which rarely matters.)
    [[nodiscard]] std::vector<Choice> moves(const chess::Board& board);

    // One of them, at random in proportion to their weights as `randomness` makes them (choiceWeights(); 0: as
    // Polyglot and Cute Chess choose); NO_MOVE when the position is not in the book.
    [[nodiscard]] chess::Move pick(const chess::Board& board, std::mt19937_64& rng, int randomness = 0);

   private:
    struct Entry {
        uint64_t key    = 0;
        uint16_t move   = 0;
        uint16_t weight = 0;
    };
    [[nodiscard]] Entry at(uint64_t i);
    [[nodiscard]] bool sorted();

    std::ifstream in_;
    uint64_t count_ = 0;         // entries
    std::vector<Entry> memory_;  // an unsorted book, sorted; empty: the entries are read from the file
};

// Whether a participant's own book still plays at `ply` (0: the first move of the game, counted from the position
// the game started from, the moves of the tournament's opening included); `depth` 0 is the whole book.
[[nodiscard]] inline bool withinBookDepth(std::size_t ply, int depth) noexcept {
    return depth <= 0 || ply < static_cast<std::size_t>(depth);
}

}  // namespace fcg::tour
