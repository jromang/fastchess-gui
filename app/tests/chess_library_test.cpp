#include <string>

#include <chess.hpp>
#include <doctest/doctest.hpp>

namespace fastchess {

// What the opening books give the chess library: malformed positions and moves are refused, not read past the board.
TEST_SUITE("Chess Library Input") {
    TEST_CASE("setFen refuses a placement that is not eight ranks of eight squares") {
        chess::Board board;

        CHECK_FALSE(board.setFen("rnbq/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e4"));
        CHECK_FALSE(board.setFen("2q1rrpn2/1p6/3QP3/1BN1B3/PPP3PP/R4RK1/8/7k w - - 0 1"));
        CHECK_FALSE(board.setFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR/8 w KQkq - 0 1"));
        CHECK_FALSE(board.setFen("rnbqkbnr/pppppppp/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
        CHECK_FALSE(board.setFen("rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
        CHECK_FALSE(board.setFen("rnbqkbnr/pppppppp/08/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));

        CHECK(board.setFen("rnbqkbnr/pppppppp/44/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));
        CHECK(board.getFen() == chess::constants::STARTPOS);
    }

    TEST_CASE("setFen gives no castling right without the king") {
        for (const bool frc : {false, true}) {
            chess::Board board;
            board.set960(frc);

            // No black king.
            CHECK(board.setFen("8/8/8/4P3/8/PPPP1PPP/RNBQKBNR/7R b KQkq - 0 1"));
            CHECK_FALSE(board.castlingRights().has(chess::Color::BLACK));

            // No king at all, and an en passant square.
            CHECK(board.setFen("r6r/8/8/3pP3/8/8/8/R6R w KQkq d6 0 1"));
            CHECK(board.castlingRights().isEmpty());
            CHECK(board.enpassantSq() == chess::Square::NO_SQ);

            // The king off its back rank.
            CHECK(board.setFen("r6r/8/4k3/8/8/4K3/8/R6R w KQkq - 0 1"));
            CHECK(board.castlingRights().isEmpty());
        }
    }

    TEST_CASE("setFen in Chess960 reads only the files of the rooks as castling rights") {
        chess::Board board;
        board.set960(true);

        CHECK(board.setFen("1k1r4/pp1b1R2/3q2pp/4p3/2B5/4Q3/PPP2B2/2K5 b \xe8- - 0 1"));
        CHECK(board.castlingRights().isEmpty());

        CHECK(board.setFen("r3k2r/8/8/8/8/8/8/R3K2R w ZzEe - 0 1"));
        CHECK(board.castlingRights().isEmpty());

        CHECK(board.setFen("bqnnrbkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w HFhe - 0 1"));
        CHECK(board.getFen() == "bqnnrbkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w HFhe - 0 1");
    }

    TEST_CASE("parseSan refuses a malformed castling or promotion") {
        const chess::Board board("r3k2r/1P6/8/8/8/8/8/R3K2R w KQkq - 0 1");

        for (const auto* san : {"O-", "0-", "01k-", "O-O-", "O-O-0", "Ox", "b8="}) {
            CAPTURE(san);
            CHECK_THROWS_AS((void)chess::uci::parseSan(board, san), chess::uci::SanParseError);
        }

        CHECK(chess::uci::parseSan(board, "O-O") ==
              chess::Move::make<chess::Move::CASTLING>(chess::Square::SQ_E1, chess::Square::SQ_H1));
        CHECK(chess::uci::parseSan(board, "0-0-0+") ==
              chess::Move::make<chess::Move::CASTLING>(chess::Square::SQ_E1, chess::Square::SQ_A1));
        CHECK(chess::uci::parseSan(board, "b8=Q") ==
              chess::Move::make<chess::Move::PROMOTION>(chess::Square::SQ_B7, chess::Square::SQ_B8,
                                                        chess::PieceType::QUEEN));
    }
}

}  // namespace fastchess
