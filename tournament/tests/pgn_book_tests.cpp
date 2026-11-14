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

TEST_CASE("PGN book: a game from a position the moves cannot be read in is left out") {
    // Found by fuzzing: a FEN tag without a king, or with too many squares, made the move generator read outside its
    // tables (and the board reader write past the board).
    const auto dir = fs::temp_directory_path() / "fcg-pgn-book-bad-fen";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "bad.pgn", std::ios::binary)
        << "[FEN \"8/8/8/8/8/8/4P3/8 w - - 0 1\"]\n\n1. e4 *\n\n"
           "[FEN \"rnbqkbnrr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\"]\n\n1. e4 *\n\n"
           "[FEN \"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR/8 w KQkq - 0 1\"]\n\n1. e4 *\n\n"
           "[Event \"good\"]\n\n1. e4 e5 *\n";
    const auto stats = pgnToPolyglot(dir / "bad.pgn", dir / "bad.bin", 8, false);
    CHECK(stats.games == 1);
    CHECK(stats.skipped == 3);
    fs::remove_all(dir);
}

namespace {

// Five games with results and ratings, (White, Black, result): 2500-2300 1-0, 2450-2600 0-1, 2200-2200 drawn,
// 2700-2700 without a result, unrated 1-0; and one from a FEN with Black to move, 2000-2500 0-1.
fs::path ratedPgn(const fs::path& dir) {
    fs::create_directories(dir);
    const auto path = dir / "rated.pgn";
    std::ofstream(path, std::ios::binary)
        << "[Result \"1-0\"]\n[WhiteElo \"2500\"]\n[BlackElo \"2300\"]\n\n1. e4 e5 2. Nf3 Nc6 1-0\n\n"
           "[Result \"0-1\"]\n[WhiteElo \"2450\"]\n[BlackElo \"2600\"]\n\n1. e4 c5 2. Nf3 d6 0-1\n\n"
           "[Result \"1/2-1/2\"]\n[WhiteElo \"2200\"]\n[BlackElo \"2200\"]\n\n1. d4 d5 2. c4 e6 1/2-1/2\n\n"
           "[Result \"*\"]\n[WhiteElo \"2700\"]\n[BlackElo \"2700\"]\n\n1. e4 e5 2. Bc4 Nf6 *\n\n"
           "[Result \"1-0\"]\n[WhiteElo \"-\"]\n\n1. e4 e5 2. Nf3 Nf6 1-0\n\n"
           "[Result \"0-1\"]\n[WhiteElo \"2000\"]\n[BlackElo \"2500\"]\n[SetUp \"1\"]\n[FEN \""
        << CASTLING << "\"]\n\n1... O-O-O 2. Kf2 0-1\n";
    return path;
}

PgnBookOptions bookOptions(int plies, int min_games, PgnBookOptions::Weight weight, int min_elo) {
    PgnBookOptions o;
    o.plies     = plies;
    o.min_games = min_games;
    o.weight    = weight;
    o.min_elo   = min_elo;
    return o;
}

}  // namespace

TEST_CASE("PGN book options: the depth and the minimum number of games per move") {
    const auto dir = fs::temp_directory_path() / "fcg-pgn-book-options";
    fs::remove_all(dir);
    const auto pgn = ratedPgn(dir);
    const auto bin = dir / "rated.bin";
    using W        = PgnBookOptions::Weight;

    // Every move, by count: the game without a result counts.
    auto stats = pgnToPolyglot(pgn, bin, bookOptions(40, 1, W::Count, 0));
    CHECK(stats.games == 6);
    CHECK(stats.left_out == 0);
    auto raw = readRaw(bin);
    CHECK(weightOf(raw, polyglotKey(""), "e2e4") == 4);
    CHECK(weightOf(raw, polyglotKey(""), "d2d4") == 1);
    CHECK(weightOf(raw, polyglotKey("e2e4"), "e7e5") == 3);
    CHECK(weightOf(raw, polyglotKey("e2e4 e7e5"), "g1f3") == 2);
    CHECK(weightOf(raw, polyglotKey("e2e4 e7e5 g1f3"), "b8c6") == 1);

    // One ply: only the first moves.
    stats = pgnToPolyglot(pgn, bin, bookOptions(1, 1, W::Count, 0));
    raw   = readRaw(bin);
    CHECK(weightOf(raw, polyglotKey(""), "e2e4") == 4);
    CHECK(entriesAt(raw, polyglotKey("e2e4")) == 0);
    CHECK(weightOf(raw, polyglotKey("", CASTLING), "e8a8") == 1);
    CHECK(entriesAt(raw, polyglotKey("e8c8", CASTLING)) == 0);
    CHECK(stats.positions == 2);
    CHECK(raw.size() == 3);

    // Two games at least: the moves played once are left out, and the positions left without a move.
    stats = pgnToPolyglot(pgn, bin, bookOptions(40, 2, W::Count, 0));
    raw   = readRaw(bin);
    CHECK(weightOf(raw, polyglotKey(""), "e2e4") == 4);
    CHECK(weightOf(raw, polyglotKey(""), "d2d4") == -1);
    CHECK(weightOf(raw, polyglotKey("e2e4"), "e7e5") == 3);
    CHECK(weightOf(raw, polyglotKey("e2e4"), "c7c5") == -1);
    CHECK(weightOf(raw, polyglotKey("e2e4 e7e5"), "g1f3") == 2);
    CHECK(weightOf(raw, polyglotKey("e2e4 e7e5"), "f1c4") == -1);
    CHECK(entriesAt(raw, polyglotKey("e2e4 e7e5 g1f3")) == 0);
    CHECK(stats.positions == 3);
    CHECK(stats.entries == 3);
    CHECK(raw.size() == 3);

    // More than any move: nothing is left, and the reason says so.
    try {
        (void)pgnToPolyglot(pgn, bin, bookOptions(40, 10, W::Count, 0));
        FAIL("no exception");
    } catch (const EmptyPgnBook& e) {
        CHECK(std::string(e.what()).find("10 games or more") != std::string::npos);
        CHECK(e.stats.games == 6);
    }
    fs::remove_all(dir);
}

TEST_CASE("PGN book options: weights by score for the side that moved, and the players' rating") {
    const auto dir = fs::temp_directory_path() / "fcg-pgn-book-score";
    fs::remove_all(dir);
    const auto pgn = ratedPgn(dir);
    const auto bin = dir / "rated.bin";
    using W        = PgnBookOptions::Weight;

    // A win 2, a draw 1, a loss 0, for the side that played the move; the game without a result left out.
    auto stats = pgnToPolyglot(pgn, bin, bookOptions(40, 1, W::Score, 0));
    CHECK(stats.games == 5);
    CHECK(stats.left_out == 1);
    auto raw = readRaw(bin);
    for (std::size_t i = 1; i < raw.size(); ++i) {
        CHECK(raw[i - 1].key <= raw[i].key);
        if (raw[i - 1].key == raw[i].key) CHECK(raw[i - 1].weight >= raw[i].weight);
    }
    CHECK(weightOf(raw, polyglotKey(""), "e2e4") == 4);  // won, lost, won
    CHECK(weightOf(raw, polyglotKey(""), "d2d4") == 1);  // drawn
    CHECK(weightOf(raw, polyglotKey("e2e4"), "c7c5") == 2);
    CHECK(weightOf(raw, polyglotKey("e2e4"), "e7e5") == -1);  // lost twice: weighs nothing, left out
    CHECK(weightOf(raw, polyglotKey("e2e4 e7e5"), "g1f3") == 4);
    CHECK(weightOf(raw, polyglotKey("e2e4 e7e5"), "f1c4") == -1);  // only in the game without a result
    CHECK(entriesAt(raw, polyglotKey("e2e4 c7c5")) == 0);          // 2. Nf3 lost
    CHECK(weightOf(raw, polyglotKey("", CASTLING), "e8a8") == 2);  // Black won
    CHECK(weightOf(raw, polyglotKey("d2d4 d7d5"), "c2c4") == 1);

    // Players rated 2400 or more, by count: the moves of each side by its own rating.
    stats = pgnToPolyglot(pgn, bin, bookOptions(40, 1, W::Count, 2400));
    CHECK(stats.games == 4);     // the first, the second, the one without a result, the one from a FEN
    CHECK(stats.left_out == 2);  // 2200-2200, and the unrated one
    raw = readRaw(bin);
    CHECK(weightOf(raw, polyglotKey(""), "e2e4") == 3);
    CHECK(weightOf(raw, polyglotKey(""), "d2d4") == -1);
    CHECK(weightOf(raw, polyglotKey("e2e4"), "e7e5") == 1);  // not the 2300's of the first game
    CHECK(weightOf(raw, polyglotKey("e2e4"), "c7c5") == 1);
    CHECK(weightOf(raw, polyglotKey("e2e4 e7e5"), "g1f3") == 1);
    CHECK(entriesAt(raw, polyglotKey("e2e4 e7e5 g1f3")) == 0);
    CHECK(weightOf(raw, polyglotKey("", CASTLING), "e8a8") == 1);  // Black to move first, rated 2500
    CHECK(entriesAt(raw, polyglotKey("e8c8", CASTLING)) == 0);     // White, 2000

    // Both: by score, the rated players' moves of the games with a result.
    stats = pgnToPolyglot(pgn, bin, bookOptions(40, 1, W::Score, 2400));
    CHECK(stats.games == 3);
    CHECK(stats.left_out == 3);
    raw = readRaw(bin);
    CHECK(weightOf(raw, polyglotKey(""), "e2e4") == 2);
    CHECK(weightOf(raw, polyglotKey("e2e4"), "c7c5") == 2);
    CHECK(entriesAt(raw, polyglotKey("e2e4")) == 1);

    // Rated above every player: no game is left.
    CHECK_THROWS_AS(pgnToPolyglot(pgn, bin, bookOptions(40, 1, W::Count, 3000)), EmptyPgnBook);
    fs::remove_all(dir);
}
