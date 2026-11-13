#pragma once

// A PGN opening book as a participant's own book (see book_adapter.hpp). The adapter reads only Polyglot books, so
// the PGN book is turned once into one: the moves of its games up to the participant's depth, each (position, move)
// weighted by the number of games that play it. The result is kept in a cache of the profile, named after the PGN
// file (its path, size and time), the depth and the variant, and made again only when one of these changes.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <chess.hpp>

namespace fcg::tour {

// "All of it" for a PGN book: the first 40 plies of its games. A Polyglot book ends where its author ended it; the
// games of a PGN book go on to their results, and their middlegames would make a large book of lines played once.
inline constexpr int PGN_BOOK_ALL_PLIES = 40;

// The plies of each game a PGN book is turned into for `depth` (0: all of it).
[[nodiscard]] constexpr int pgnBookPlies(int depth) noexcept { return depth > 0 ? depth : PGN_BOOK_ALL_PLIES; }

[[nodiscard]] bool isPgnBook(const std::filesystem::path& file);

// One entry of a Polyglot book.
struct PolyglotEntry {
    uint64_t key    = 0;
    uint16_t move   = 0;
    uint16_t weight = 0;
};

// A move as the format writes it: from, to, the promotion's piece; castling as the king taking its rook (which is
// also how chess-library holds it).
[[nodiscard]] uint16_t polyglotMove(chess::Move move);

// Writes `entries` as a Polyglot book: sorted by key, the moves of a position by weight, the heaviest first. Written
// to a temporary file renamed at the end, so that a book being read is never seen half written. Throws
// std::runtime_error with the reason.
void writePolyglotBook(const std::filesystem::path& file, std::vector<PolyglotEntry> entries);

struct PgnBookStats {
    uint64_t games     = 0;  // games whose moves were counted
    uint64_t skipped   = 0;  // games left out: an illegal or unreadable move, a starting position that cannot be set
    uint64_t positions = 0;  // in the Polyglot book
    uint64_t entries   = 0;  // (position, move) pairs in it
    std::string stopped;     // why the reading stopped before the end of the file (the games before counted); empty
};

// The conversion was cancelled (its progress callback returned false).
struct BookCancelled : std::runtime_error {
    BookCancelled() : std::runtime_error("cancelled") {}
};

// The share of the PGN file read so far, 0 to 1; returns false to cancel.
using BookProgress = std::function<bool(double)>;

// Turns the PGN book `pgn` into the Polyglot book `bin`: each game played from its start (its FEN tag if any;
// Chess960 with `chess960` or when its Variant tag says so) for `plies` plies, each (position, move) counted, and
// the counts become the weights (scaled to the format's 16 bits per position when a move is played more than 65535
// times, their ratios kept). The games are read one at a time: the memory is that of the distinct (position, move)
// pairs, not of the file. A game with an illegal move is left out and counted; a reading error that stops the
// parser ends the reading, the games before it kept. Throws std::runtime_error when the file cannot be read or no
// game gives a move, BookCancelled when cancelled.
PgnBookStats pgnToPolyglot(const std::filesystem::path& pgn, const std::filesystem::path& bin, int plies, bool chess960,
                           const BookProgress& progress = {});

// The folder of the cache: <profile>/book-cache, set at the program's start. Empty: a folder in the system's
// temporary folder.
inline std::filesystem::path book_cache_dir;

// A PGN book turned into a Polyglot book.
struct PreparedBook {
    std::string file;  // the Polyglot book
    PgnBookStats stats;
    bool reused = false;  // found in the cache, not made now
};

// Where the Polyglot book of `pgn` for `depth` is kept in the cache, whether it is there or not; empty when the PGN
// file cannot be found.
[[nodiscard]] std::filesystem::path pgnBookCacheFile(const std::string& pgn, int depth, bool chess960);

// The Polyglot book of `pgn` for `depth`, when the cache has it for the file as it is now.
[[nodiscard]] std::optional<PreparedBook> cachedPgnBook(const std::string& pgn, int depth, bool chess960);

// The same, made now when the cache does not have it (see pgnToPolyglot(), which throws as it does).
PreparedBook preparePgnBook(const std::string& pgn, int depth, bool chess960, const BookProgress& progress = {});

// The Polyglot book a participant's own `book` plays from: the book itself when it is one; for a PGN book, the one
// it was turned into, from the cache. Not made here: when the PGN book was not prepared, the place it would be (the
// adapter then reports that it cannot read it). A tournament asks for its participants' books when it is opened,
// right after their preparation, and keeps the answers while it is open, so that a PGN file changed meanwhile does
// not change the book its games play from (nor makes them look for one never made).
[[nodiscard]] std::string ownBookFile(const std::string& book, int depth, bool chess960);

// Marks the cached Polyglot book `file` used, when it was last marked more than a day ago: kept by
// pruneBookCache() for another while. A tournament does so at each game of a participant whose book it is, so that
// a tournament that plays for weeks keeps its books whatever another program on the same profile prunes meanwhile.
void keepCachedBook(const std::string& file);

// Removes from the cache the books not used for `days` days (a book is used when it is found in the cache or made).
// Returns how many files were removed.
std::size_t pruneBookCache(int days = 30);

}  // namespace fcg::tour
