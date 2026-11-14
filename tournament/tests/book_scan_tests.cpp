#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>

#include <tour/book_scan.hpp>
#include <tour/chess_text.hpp>

using namespace fcg::tour;
namespace fs = std::filesystem;

namespace {

fs::path write(const std::string& name, const std::string& text) {
    const auto path = fs::temp_directory_path() / name;
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    return path;
}

}  // namespace

TEST_CASE("book scan: an EPD book's illegal, duplicate, mirrored and finished positions") {
    const auto book = write("fcg-scan.epd",
                            "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - c0 \"1.e4\";\n"
                            "rnbqkbnr/pppp1ppp/8/4p3/8/8/PPPPPPPP/RNBQKBNR w KQkq -\n"         // 1.e4 mirrored
                            "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - hmvc 0;\n"  // 1.e4 again
                            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKKNR w KQkq -\n"            // two white kings
                            "rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq -\n"       // mate
                            "8/8/8/4k3/8/8/8/4K3 w - -\n");                                      // bare kings
    // fastchess' reader leaves out the position with two white kings: it counts as illegal, not as an opening.
    const BookScan scan(book, -1);
    CHECK(scan.kind() == BookScan::Kind::Epd);
    CHECK_FALSE(scan.chess960());
    REQUIRE(scan.size() == 5);
    CHECK(scan.flags(0) == 0);
    CHECK(scan.flags(1) == BOOK_MIRRORED);
    CHECK(scan.flags(2) == BOOK_DUPLICATE);
    CHECK(scan.flags(3) == BOOK_OVER);
    CHECK(scan.flags(4) == BOOK_OVER);
    CHECK(scan.count(BOOK_ILLEGAL) == 1);
    CHECK(scan.count(BOOK_DUPLICATE | BOOK_MIRRORED) == 2);
    CHECK(scan.item(0).fen == "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -");

    const auto clean = fs::temp_directory_path() / "fcg-scan-clean.epd";
    CHECK(scan.writeEpd(clean, BOOK_ILLEGAL | BOOK_DUPLICATE | BOOK_OVER) == 2);
    CHECK(BookScan(clean, -1).size() == 2);
}

TEST_CASE("book scan: a PGN book gives the position after its moves, and the moves in SAN") {
    const auto book = write("fcg-scan.pgn",
                            "[Event \"a\"]\n\n1. e4 e5 2. Nf3 Nc6 *\n\n"
                            "[Event \"b\"]\n\n1. d4 d5 *\n\n"
                            "[Event \"c\"]\n\n1. Nf3 Nc6 2. e4 e5 *\n");  // the first, by transposition
    const BookScan scan(book, -1);
    CHECK(scan.kind() == BookScan::Kind::Pgn);
    REQUIRE(scan.size() == 3);
    const auto first = scan.item(0);
    CHECK(first.san == std::vector<std::string>{"e4", "e5", "Nf3", "Nc6"});
    CHECK(first.fen == "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq -");
    CHECK(scan.flags(2) == BOOK_DUPLICATE);

    CHECK(BookScan(book, 2).item(0).san.size() == 2);  // cut at 2 plies
}

TEST_CASE("book scan: the escape lines of a PGN book are ignored, and counted") {
    // PGN standard 6: a '%' in the first column makes the line a comment, here before the games, between the
    // headers and the moves, inside the move text and inside the headers.
    const auto book = write("fcg-scan-escape.pgn",
                            "% an opening book [v1]\n% made by a tool\n\n"
                            "[Event \"a\"]\n\n% before the moves\n1. e4 e5 2. Nf3 Nc6 *\n\n"
                            "[Event \"b\"]\n\n1. d4 d5\r\n% inside the moves\r\n2. c4 *\n% between games\n\n"
                            "[Event \"c\"]\n% inside the headers\n[Round \"3\"]\n\n1. c4 c5 *\n");
    const BookScan scan(book, -1);
    REQUIRE(scan.size() == 3);
    CHECK(scan.item(0).san == std::vector<std::string>{"e4", "e5", "Nf3", "Nc6"});
    CHECK(scan.item(1).san == std::vector<std::string>{"d4", "d5", "c4"});
    CHECK(scan.item(2).san == std::vector<std::string>{"c4", "c5"});
    CHECK(scan.escapeLines() == 6);
    CHECK(scan.count(BOOK_ILLEGAL | BOOK_DUPLICATE | BOOK_MIRRORED | BOOK_OVER) == 0);

    CHECK(BookScan(write("fcg-scan-plain.pgn", "[Event \"a\"]\n\n1. e4 e5 *\n"), -1).escapeLines() == 0);
}

TEST_CASE("book scan: Chess960 positions are recognised; unreadable books are reported") {
    const auto book = write("fcg-scan960.epd", "bbqnnrkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w HFhf -\n");
    const BookScan scan(book, -1);
    CHECK(scan.chess960());
    CHECK(scan.flags(0) == 0);

    CHECK_THROWS((void)BookScan(fs::temp_directory_path() / "fcg-no-such-book.epd", -1));
    CHECK_THROWS_WITH((void)BookScan(write("fcg-scan-empty.epd", ""), -1), doctest::Contains("empty"));
}

TEST_CASE("book scan: a FEN the board reader would take off the board is not read") {
    CHECK(readableFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
    CHECK(readableFen("  8/8/8/4k3/8/8/8/4K3 w - -"));
    CHECK(readableFen("4k3/8/8/8/8/8/8/4K3"));
    CHECK(readableFen("bqnb1rkr/pp3ppp/3ppn2/2p5/5P2/P2P4/NPP1P1PP/BQ1BNRKR w HFhf - 2 9"));
    CHECK_FALSE(readableFen("rnbqkbnrr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -"));  // nine squares
    CHECK_FALSE(readableFen("rnbqkbn/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -"));    // seven
    CHECK_FALSE(readableFen("8/8/8/8/8/8/8/8/8 w - -"));                                // nine ranks
    CHECK_FALSE(readableFen("8/8/8/8/8/8/8 w - -"));                                    // seven
    CHECK_FALSE(readableFen("9/8/8/8/8/8/8/8 w - -"));
    CHECK_FALSE(readableFen("x7/8/8/8/8/8/8/8 w - -"));
    CHECK_FALSE(readableFen(""));
    CHECK_FALSE(readableFen("4k3/8/8/8/8/8/8/4K3 b \xe8 -"));            // a castling right in another byte
    CHECK_FALSE(readableFen("4k3/8/8/8/8/8/8/4K3 b KQkqK -"));           // five of them
    CHECK_FALSE(readableFen("4k3/8/8/8/8/8/8/4K3 w - - 0 2000000000"));  // a move number doubled past an int
    CHECK_FALSE(readableFen("rnbqkbnr/pp1ppppp/8/2p5/4P3/5N2/PPPP1PPP/RNBnbqkb b KQkq -"));  // no white king
    CHECK_FALSE(readableFen("rnbqKbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6"));   // no black king

    // Found by fuzzing: such lines of an EPD book made the board reader compute squares off the board. fastchess'
    // reader now leaves out the first two; the third it reads, its castling right ignored, but it is flagged.
    const auto book = write("fcg-scan-squares.epd",
                            "rnbqkbnrr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -\n"
                            "8/8/8/8/8/8/8/8/8/4k3/4K3 w - -\n"
                            "1k1r4/pp1b1R2/3q2pp/4p3/2B5/4Q3/PPP2B2/2K5 b \xe8 -\n"
                            "8/8/8/4k3/8/8/8/4K3 w - -\n");
    const BookScan scan(book, -1);
    REQUIRE(scan.size() == 2);
    CHECK(scan.flags(0) == BOOK_ILLEGAL);
    CHECK(scan.flags(1) == BOOK_OVER);
    CHECK(scan.count(BOOK_ILLEGAL) == 3);
}

TEST_CASE("chess text: a SAN that starts as castling does must be castling") {
    for (const char* san : {"e4", "Nf3", "exd5", "O-O", "O-O-O", "0-0", "0-0-0+", "O-O#", "O-O-O!?", ""}) {
        CHECK_MESSAGE(readableSan(san), san);
    }
    for (const char* san : {"01k-", "O-", "O", "0-O", "O-O-", "O-O-0", "O-Ox", "0-0-0-0", "Ok"}) {
        CHECK_FALSE_MESSAGE(readableSan(san), san);
    }
}

TEST_CASE("chess text: a king in check by three pieces is not a legal position") {
    chess::Board board;
    REQUIRE(board.setFen("4k3/8/3N4/8/Q7/8/8/4R2K b - - 0 1"));
    CHECK_FALSE(legalPosition(board));
    REQUIRE(board.setFen("4k3/8/3N4/8/8/8/8/4R2K b - - 0 1"));  // two, a double check
    CHECK(legalPosition(board));
}
