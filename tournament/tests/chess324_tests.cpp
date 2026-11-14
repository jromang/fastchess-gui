// Chess324: the 324 starting positions an EPD book holds, and the 18 symmetrical ones.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <chess.hpp>

#include <tour/book_scan.hpp>
#include <tour/chess324.hpp>
#include <tour/chess_text.hpp>

using namespace fcg::tour;
namespace fs = std::filesystem;

namespace {

// Whether two squares of the same rank are of opposite colours.
bool oppositeColours(chess::Square a, chess::Square b) { return (a.index() + b.index()) % 2 != 0; }

// The squares of `piece` on the board.
std::vector<chess::Square> squaresOf(const chess::Board& board, chess::Piece piece) {
    std::vector<chess::Square> out;
    for (int sq = 0; sq < 64; ++sq) {
        if (board.at(chess::Square(sq)) == piece) out.emplace_back(sq);
    }
    return out;
}

}  // namespace

TEST_CASE("Chess324: 18 arrangements of a back rank, kings and rooks in place, bishops on opposite colours") {
    const auto ranks = chess324Ranks();
    CHECK(ranks.size() == 18);
    CHECK(std::set<std::string>(ranks.begin(), ranks.end()).size() == 18);
    CHECK(ranks.front() == "RNBQKBNR");
    for (const auto& r : ranks) {
        CAPTURE(r);
        REQUIRE(r.size() == 8);
        CHECK(r[0] == 'R');
        CHECK(r[4] == 'K');
        CHECK(r[7] == 'R');
        CHECK(std::count(r.begin(), r.end(), 'Q') == 1);
        CHECK(std::count(r.begin(), r.end(), 'N') == 2);
        REQUIRE(std::count(r.begin(), r.end(), 'B') == 2);
        const auto first = r.find('B'), second = r.rfind('B');
        CHECK((second - first) % 2 == 1);
    }
}

TEST_CASE("Chess324: 324 distinct legal positions, the usual castling rights, White and Black independent") {
    const auto positions = chess324Positions(false);
    CHECK(positions.size() == 324);
    CHECK(std::set<std::string>(positions.begin(), positions.end()).size() == 324);
    CHECK(positions.front() == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -");

    std::set<uint64_t> keys;
    std::set<std::string> whites, blacks;
    for (const auto& fen : positions) {
        CAPTURE(fen);
        REQUIRE(readableFen(fen));
        chess::Board board;
        REQUIRE(board.setFen(fen));
        CHECK(legalPosition(board));
        CHECK(board.sideToMove() == chess::Color::WHITE);
        keys.insert(board.hash());

        using Side        = chess::Board::CastlingRights::Side;
        const auto rights = board.castlingRights();
        for (const auto colour : {chess::Color::WHITE, chess::Color::BLACK}) {
            CHECK(rights.has(colour, Side::KING_SIDE));
            CHECK(rights.has(colour, Side::QUEEN_SIDE));
            CHECK(rights.getRookFile(colour, Side::KING_SIDE) == chess::File::FILE_H);
            CHECK(rights.getRookFile(colour, Side::QUEEN_SIDE) == chess::File::FILE_A);
        }
        // Kings and rooks where castling wants them.
        using chess::Piece;
        CHECK(board.at(chess::Square("e1")) == Piece::WHITEKING);
        CHECK(board.at(chess::Square("e8")) == Piece::BLACKKING);
        CHECK(board.at(chess::Square("a1")) == Piece::WHITEROOK);
        CHECK(board.at(chess::Square("h1")) == Piece::WHITEROOK);
        CHECK(board.at(chess::Square("a8")) == Piece::BLACKROOK);
        CHECK(board.at(chess::Square("h8")) == Piece::BLACKROOK);
        for (const auto bishop : {Piece::WHITEBISHOP, Piece::BLACKBISHOP}) {
            const auto squares = squaresOf(board, bishop);
            REQUIRE(squares.size() == 2);
            CHECK(oppositeColours(squares[0], squares[1]));
        }
        CHECK(squaresOf(board, Piece::WHITEQUEEN).size() == 1);
        CHECK(squaresOf(board, Piece::BLACKKNIGHT).size() == 2);

        whites.insert(fen.substr(fen.rfind('/') + 1, 8));
        blacks.insert(fen.substr(0, 8));
    }
    CHECK(keys.size() == 324);
    CHECK(whites.size() == 18);
    CHECK(blacks.size() == 18);
}

TEST_CASE("Chess324: the 18 symmetrical positions, Black's arrangement mirroring White's") {
    const auto symmetrical = chess324Positions(true);
    const auto all         = chess324Positions(false);
    CHECK(symmetrical.size() == 18);
    const std::set<std::string> every(all.begin(), all.end());
    for (const auto& fen : symmetrical) {
        CAPTURE(fen);
        CHECK(every.count(fen) == 1);
        auto black = fen.substr(0, 8);
        for (auto& c : black) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        CHECK(black == fen.substr(fen.rfind('/') + 1, 8));
    }
    CHECK(std::set<std::string>(symmetrical.begin(), symmetrical.end()).size() == 18);
}

TEST_CASE("Chess324: the book written reads as 324 openings without a problem, and not as Chess960") {
    const auto dir = fs::temp_directory_path() / "fcg-chess324";
    fs::remove_all(dir);
    const auto file = dir / "books" / "chess324.epd";  // its folder made
    CHECK(writeChess324(file, false) == 324);
    const BookScan scan(file, -1);
    CHECK(scan.kind() == BookScan::Kind::Epd);
    CHECK(scan.size() == 324);
    CHECK(scan.count(BOOK_ILLEGAL | BOOK_DUPLICATE | BOOK_MIRRORED | BOOK_OVER) == 0);
    CHECK_FALSE(scan.chess960());

    CHECK(writeChess324(dir / "symmetrical.epd", true) == 18);
    const BookScan symmetrical(dir / "symmetrical.epd", -1);
    CHECK(symmetrical.size() == 18);
    CHECK(symmetrical.count(BOOK_ILLEGAL | BOOK_DUPLICATE | BOOK_MIRRORED | BOOK_OVER) == 0);
    fs::remove_all(dir);
}
