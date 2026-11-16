// The notation of the Board page: figurines for the pieces, a line of UCI moves as items, and what the analysis reads
// of an info line.

#include <doctest/doctest.hpp>

#include "analysis.hpp"
#include "notation_view.hpp"

using namespace fcg;

TEST_CASE("notation: a piece's letter as a figurine, pawns and castling as they are") {
    auto f = figurineOf("Nf3");
    REQUIRE(f);
    CHECK(f->type == chess::PieceType::KNIGHT);
    CHECK(f->rest == "f3");
    f = figurineOf("Qxd8+");
    REQUIRE(f);
    CHECK(f->type == chess::PieceType::QUEEN);
    CHECK(f->rest == "xd8+");
    CHECK_FALSE(figurineOf("e4"));
    CHECK_FALSE(figurineOf("exd5"));
    CHECK_FALSE(figurineOf("O-O"));
    CHECK_FALSE(figurineOf("e8=Q"));
    CHECK_FALSE(figurineOf(""));
}

TEST_CASE("notation: a line of moves as items, numbered, cut at an illegal move or the most asked") {
    chess::Board board;
    board.setFen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    auto items = lineItems(board, {"e7e5", "g1f3", "b8c6", "f1b5"}, 10);
    // 1…e5 2.Nf3 Nc6 3.Bb5
    REQUIRE(items.size() == 7);
    CHECK(items[0].kind == NotationItem::Kind::Number);
    CHECK(items[0].text == "1…");
    CHECK(items[1].text == "e5");
    CHECK(items[1].id == 0);
    CHECK(items[2].text == "2.");
    CHECK(items[3].text == "Nf3");
    CHECK(items[4].text == "Nc6");
    CHECK(items[4].id == 2);
    CHECK(items[6].text == "Bb5");
    CHECK(lineItems(board, {"e7e5", "e1e3"}, 10).size() == 2);  // the king cannot go there
    CHECK(lineItems(board, {"e7e5", "g1f3", "b8c6"}, 1).size() == 2);
}

TEST_CASE("analysis: an info line's tablebase hits and hash fill are read") {
    std::vector<AnalysisLine> lines;
    REQUIRE(takeInfoLine(lines,
                         "info depth 12 seldepth 20 multipv 1 score cp 31 nodes 1000 nps 50000 hashfull 412 "
                         "tbhits 7 time 20 pv e2e4 e7e5",
                         1));
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].tbhits == 7);
    CHECK(lines[0].hashfull == 412);
    std::vector<AnalysisLine> none;
    REQUIRE(takeInfoLine(none, "info depth 1 score cp 0 pv e2e4", 1));
    CHECK(none[0].hashfull == -1);
}
