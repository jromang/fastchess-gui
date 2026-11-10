#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>

#include <tour/book_scan.hpp>

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
    const BookScan scan(book, -1);
    CHECK(scan.kind() == BookScan::Kind::Epd);
    CHECK_FALSE(scan.chess960());
    REQUIRE(scan.size() == 6);
    CHECK(scan.flags(0) == 0);
    CHECK(scan.flags(1) == BOOK_MIRRORED);
    CHECK(scan.flags(2) == BOOK_DUPLICATE);
    CHECK(scan.flags(3) == BOOK_ILLEGAL);
    CHECK(scan.flags(4) == BOOK_OVER);
    CHECK(scan.flags(5) == BOOK_OVER);
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

TEST_CASE("book scan: Chess960 positions are recognised; unreadable books are reported") {
    const auto book = write("fcg-scan960.epd", "bbqnnrkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w HFhf -\n");
    const BookScan scan(book, -1);
    CHECK(scan.chess960());
    CHECK(scan.flags(0) == 0);

    CHECK_THROWS((void)BookScan(fs::temp_directory_path() / "fcg-no-such-book.epd", -1));
    CHECK_THROWS_WITH((void)BookScan(write("fcg-scan-empty.epd", ""), -1), doctest::Contains("empty"));
}
