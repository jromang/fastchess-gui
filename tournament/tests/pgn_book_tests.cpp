// A PGN book as a participant's own book: turned into a Polyglot book, kept in the cache.

#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include <tour/pgn_book.hpp>
#include <tour/polyglot.hpp>

#include "polyglot_writer.hpp"

using namespace fcg::tour;
using fcg::test::polyglotKey;
namespace fs = std::filesystem;

namespace {

// The entries of a Polyglot book as written: (key, move, weight).
struct Raw {
    uint64_t key;
    uint16_t move;
    uint16_t weight;
};
std::vector<Raw> readRaw(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::vector<Raw> out;
    unsigned char b[16];
    while (in.read(reinterpret_cast<char*>(b), 16)) {
        uint64_t key = 0;
        for (int i = 0; i < 8; ++i) key = (key << 8) | b[i];
        out.push_back({key, static_cast<uint16_t>((b[8] << 8) | b[9]), static_cast<uint16_t>((b[10] << 8) | b[11])});
    }
    return out;
}

// The weight the book gives `uci` (Polyglot's own notation: castling as the king takes its rook) at `key`; -1 when
// it has no such entry.
int weightOf(const std::vector<Raw>& raw, uint64_t key, const std::string& uci) {
    for (const auto& r : raw) {
        if (r.key == key && r.move == fcg::test::polyglotMove(uci)) return r.weight;
    }
    return -1;
}

std::size_t entriesAt(const std::vector<Raw>& raw, uint64_t key) {
    return static_cast<std::size_t>(std::count_if(raw.begin(), raw.end(), [&](const Raw& r) { return r.key == key; }));
}

const char* const PROMOTION = "8/P7/8/8/8/8/8/k6K w - - 0 1";
const char* const CASTLING  = "r3k3/8/8/8/8/8/8/4K2R b q - 0 1";

// Four games from the initial position, one with an illegal move (left out), two from a FEN.
fs::path smallPgn(const fs::path& dir) {
    fs::create_directories(dir);
    const auto path = dir / "small.pgn";
    std::ofstream(path, std::ios::binary) << "[Event \"a\"]\n[Result \"*\"]\n\n"
                                             "1. e4 e5 2. Nf3 Nc6 3. Bc4 Bc5 4. O-O Nf6 *\n\n"
                                             "[Event \"b\"]\n\n1. e4 e5 2. Nf3 Nf6 3. Nc3 *\n\n"
                                             "[Event \"c\"]\n\n1. e4 {a comment} c5 (1... e6) *\n\n"
                                             "[Event \"d\"]\n\n1. d4 d5 1-0\n\n"
                                             "[Event \"illegal\"]\n\n1. e4 e5 2. Ke3 Nf6 *\n\n"
                                             "[Event \"promotion\"]\n[SetUp \"1\"]\n[FEN \""
                                          << PROMOTION
                                          << "\"]\n\n1. a8=Q+ Kb2 *\n\n"
                                             "[Event \"castling\"]\n[SetUp \"1\"]\n[FEN \""
                                          << CASTLING << "\"]\n\n1... O-O-O 2. Kf2 *\n";
    return path;
}

}  // namespace

TEST_CASE("PGN book: the games' moves become a Polyglot book, weighted by the games that play them") {
    const auto dir = fs::temp_directory_path() / "fcg-pgn-book-convert";
    fs::remove_all(dir);
    const auto pgn = smallPgn(dir);
    const auto bin = dir / "small.bin";

    const auto stats = pgnToPolyglot(pgn, bin, 40, false);
    CHECK(stats.games == 6);
    CHECK(stats.skipped == 1);  // 2. Ke3
    CHECK(stats.stopped.empty());
    const auto raw = readRaw(bin);
    CHECK(raw.size() == stats.entries);

    // The counts: 1. e4 three times (the illegal game left out), 1. d4 once; the variation is not a game's move.
    CHECK(weightOf(raw, polyglotKey(""), "e2e4") == 3);
    CHECK(weightOf(raw, polyglotKey(""), "d2d4") == 1);
    CHECK(entriesAt(raw, polyglotKey("")) == 2);
    CHECK(weightOf(raw, polyglotKey("e2e4"), "e7e5") == 2);
    CHECK(weightOf(raw, polyglotKey("e2e4"), "c7c5") == 1);
    CHECK(weightOf(raw, polyglotKey("e2e4"), "e7e6") == -1);
    CHECK(weightOf(raw, polyglotKey("e2e4 e7e5 g1f3"), "b8c6") == 1);
    CHECK(weightOf(raw, polyglotKey("e2e4 e7e5 g1f3"), "g8f6") == 1);
    CHECK(weightOf(raw, polyglotKey("e2e4 e7e5 g1f3 g8f6"), "b1c3") == 1);

    // Castling as the king takes its rook, short and long; a promotion with its piece; from the games' FEN.
    CHECK(weightOf(raw, polyglotKey("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5"), "e1h1") == 1);
    CHECK(weightOf(raw, polyglotKey("", PROMOTION), "a7a8q") == 1);
    CHECK(weightOf(raw, polyglotKey("a7a8q", PROMOTION), "a1b2") == 1);
    CHECK(weightOf(raw, polyglotKey("", CASTLING), "e8a8") == 1);
    CHECK(weightOf(raw, polyglotKey("e8c8", CASTLING), "e1f2") == 1);

    // Sorted by key, the heaviest move of a position first.
    for (std::size_t i = 1; i < raw.size(); ++i) {
        CHECK(raw[i - 1].key <= raw[i].key);
        if (raw[i - 1].key == raw[i].key) CHECK(raw[i - 1].weight >= raw[i].weight);
    }

    // The book the adapter reads, legal moves as chess-library writes them. (Closed before the file is written
    // again: Windows does not replace a file that is open.)
    {
        PolyglotBook book(bin);
        chess::Board board;
        const auto moves = book.moves(board);
        REQUIRE(moves.size() == 2);
        CHECK(chess::uci::moveToUci(moves[0].move) == "e2e4");
        CHECK(moves[0].weight == 3);
        chess::Board promotion(PROMOTION);
        REQUIRE(book.moves(promotion).size() == 1);
        CHECK(chess::uci::moveToUci(book.moves(promotion)[0].move) == "a7a8q");
        chess::Board castling(CASTLING);
        REQUIRE(book.moves(castling).size() == 1);
        CHECK(book.moves(castling)[0].move.typeOf() == chess::Move::CASTLING);
    }

    // Two plies: the moves after 1. e4 e5 are not in the book. The game whose third move is illegal is read no
    // further than its first two, which count.
    const auto shallow_stats = pgnToPolyglot(pgn, bin, 2, false);
    CHECK(shallow_stats.games == 7);
    CHECK(shallow_stats.skipped == 0);
    const auto shallow = readRaw(bin);
    CHECK(weightOf(shallow, polyglotKey("e2e4"), "e7e5") == 3);
    CHECK(entriesAt(shallow, polyglotKey("e2e4 e7e5")) == 0);
    CHECK(entriesAt(shallow, polyglotKey("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5")) == 0);
    CHECK(weightOf(shallow, polyglotKey("a7a8q", PROMOTION), "a1b2") == 1);
    fs::remove_all(dir);
}

TEST_CASE("PGN book: a move played more than 65535 times is scaled to 16 bits, the others in proportion") {
    const auto dir = fs::temp_directory_path() / "fcg-pgn-book-scale";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto pgn = dir / "many.pgn";
    {
        std::ofstream out(pgn, std::ios::binary);
        for (int i = 0; i < 100000; ++i) out << "[Result \"*\"]\n\n1. e4 *\n\n";
        for (int i = 0; i < 3; ++i) out << "[Result \"*\"]\n\n1. d4 *\n\n";
    }
    double last      = -1;
    bool ordered     = true;
    const auto stats = pgnToPolyglot(pgn, dir / "many.bin", 8, false, [&](double share) {
        ordered = ordered && share >= last && share <= 1.0;
        last    = share;
        return true;
    });
    CHECK(stats.games == 100003);
    CHECK(ordered);
    CHECK(last == 1.0);
    const auto raw = readRaw(dir / "many.bin");
    CHECK(weightOf(raw, polyglotKey(""), "e2e4") == 65535);
    CHECK(weightOf(raw, polyglotKey(""), "d2d4") == 2);  // 3 * 65535 / 100000, rounded

    // Cancelled from the progress: nothing written.
    fs::remove(dir / "many.bin");
    CHECK_THROWS_AS(pgnToPolyglot(pgn, dir / "many.bin", 8, false, [](double) { return false; }), BookCancelled);
    CHECK_FALSE(fs::exists(dir / "many.bin"));
    fs::remove_all(dir);
}

TEST_CASE("PGN book: a file without a game that gives a move, or none, is refused with a readable message") {
    const auto dir = fs::temp_directory_path() / "fcg-pgn-book-empty";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "empty.pgn", std::ios::binary) << "[Event \"x\"]\n\n1. Ke2 *\n";
    try {
        (void)pgnToPolyglot(dir / "empty.pgn", dir / "empty.bin", 8, false);
        FAIL("no exception");
    } catch (const std::runtime_error& e) {
        CHECK(std::string(e.what()).find("no game") != std::string::npos);
    }
    CHECK_THROWS_AS(pgnToPolyglot(dir / "none.pgn", dir / "none.bin", 8, false), std::runtime_error);
    fs::remove_all(dir);
}

TEST_CASE("PGN book: the cache keeps the conversion while the file and the depth stay; another is made otherwise") {
    const auto dir = fs::temp_directory_path() / "fcg-pgn-book-cache";
    fs::remove_all(dir);
    const auto previous = book_cache_dir;
    book_cache_dir      = dir / "cache";
    const auto pgn      = smallPgn(dir).string();

    CHECK_FALSE(cachedPgnBook(pgn, 12, false));
    const auto first = preparePgnBook(pgn, 12, false);
    CHECK_FALSE(first.reused);
    CHECK(fs::path(first.file).parent_path() == book_cache_dir);
    CHECK(fs::path(first.file).filename().string().rfind("small-", 0) == 0);
    CHECK(first.stats.games == 6);

    // Unchanged: found, with what the conversion counted.
    const auto again = preparePgnBook(pgn, 12, false);
    CHECK(again.reused);
    CHECK(again.file == first.file);
    CHECK(again.stats.skipped == 1);
    CHECK(ownBookFile(pgn, 12, false) == first.file);
    CHECK(ownBookFile("/books/own.bin", 12, false) == "/books/own.bin");  // a Polyglot book is its own

    // Another depth: another book. "All of it" is 40 plies.
    const auto deeper = preparePgnBook(pgn, 20, false);
    CHECK_FALSE(deeper.reused);
    CHECK(deeper.file != first.file);
    CHECK(pgnBookCacheFile(pgn, 0, false) == pgnBookCacheFile(pgn, PGN_BOOK_ALL_PLIES, false));
    CHECK(pgnBookCacheFile(pgn, 12, true) != pgnBookCacheFile(pgn, 12, false));

    // The file changed: made again.
    {
        std::ofstream out(pgn, std::ios::binary | std::ios::app);
        out << "\n[Event \"e\"]\n\n1. c4 *\n";
    }
    CHECK_FALSE(cachedPgnBook(pgn, 12, false));
    const auto changed = preparePgnBook(pgn, 12, false);
    CHECK_FALSE(changed.reused);
    CHECK(changed.file != first.file);
    CHECK(changed.stats.games == 7);

    // Pruned when not used for a month; the others stay.
    const auto month_ago = fs::file_time_type::clock::now() - std::chrono::hours(24 * 31);
    fs::last_write_time(fs::path(first.file).replace_extension(".json"), month_ago);
    CHECK(pruneBookCache(30) == 2);
    CHECK_FALSE(fs::exists(first.file));
    CHECK(fs::exists(changed.file));
    CHECK(cachedPgnBook(pgn, 12, false));

    book_cache_dir = previous;
    fs::remove_all(dir);
}
