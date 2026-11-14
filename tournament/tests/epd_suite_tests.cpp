#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>

#include <tour/epd_suite.hpp>

using namespace fcg::tour;

TEST_CASE("EPD suite: bm, am, id, STS points and the move counters") {
    SuitePosition p;
    REQUIRE(parseSuiteLine(
        R"(1k1r4/pp1b1R2/3q2pp/4p3/2B5/4Q3/PPP2B2/2K5 b - - bm Qd1+; id "BK.01"; c0 "Qd1+=10, Qd2=3";)", p));
    CHECK(p.id == "BK.01");
    CHECK(p.fen == "1k1r4/pp1b1R2/3q2pp/4p3/2B5/4Q3/PPP2B2/2K5 b - - 0 1");
    chess::Board board(p.fen);
    const auto qd1 = chess::uci::parseSan(board, "Qd1+");
    const auto qd2 = chess::uci::parseSan(board, "Qd2");
    REQUIRE(p.best.size() == 1);
    CHECK(p.best[0] == qd1);
    CHECK(p.solvedBy(qd1));
    CHECK_FALSE(p.solvedBy(qd2));
    CHECK(p.pointsOf(qd1) == 10);
    CHECK(p.pointsOf(qd2) == 3);
    CHECK(p.best_text == "Qd1+");

    // Avoid moves only, quoted ';', clocks.
    REQUIRE(parseSuiteLine(
        R"(rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - am f3 g4; hmvc 3; fmvn 7; id "a;b";)", p));
    CHECK(p.fen == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 3 7");
    CHECK(p.id == "a;b");
    chess::Board start;
    CHECK_FALSE(p.solvedBy(chess::uci::parseSan(start, "f3")));
    CHECK(p.solvedBy(chess::uci::parseSan(start, "e4")));

    // Not a test position: no operation to judge with, a bad FEN, a comment.
    CHECK_FALSE(parseSuiteLine("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - id \"x\";", p));
    CHECK_FALSE(parseSuiteLine("not a position bm e4;", p));
    CHECK_FALSE(parseSuiteLine("# comment", p));
}

TEST_CASE("EPD suite: a position the move generator cannot be given is skipped, its moves unread") {
    // Found by fuzzing: without a king (and with too few ranks, which the FEN reader takes), reading "Qd2" made the
    // move generator read outside its tables.
    SuitePosition p;
    CHECK_FALSE(parseSuiteLine("r1b26/3QP3/1BN1B3/PP2B2/2K2 b - - bm am Qd2; id \"x\";", p));
    CHECK_FALSE(parseSuiteLine("8/8/8/8/8/8/8/K7 w - - bm Kb1;", p));
    CHECK_FALSE(parseSuiteLine("k7/8/8/8/8/8/8/K6P w - - bm Kb1;", p));  // a pawn on the first rank
    CHECK_FALSE(parseSuiteLine("k6R/8/8/8/8/8/8/K7 w - - bm Kb1;", p));  // black in check, white to move
    CHECK(parseSuiteLine("k6R/8/8/8/8/8/8/K7 b - - bm Kb7;", p));
}

TEST_CASE("EPD suite: a move the SAN reader cannot read is not given to it") {
    // Found by fuzzing: "01k-" starts as castling does, and the SAN reader took it for a king's move to no square.
    SuitePosition p;
    CHECK_FALSE(parseSuiteLine("3r1k2/4npp1/1ppr3p/p6P/P2PPPP1/1NR5/5K2/2R5 w - - bm 01k-", p));
    CHECK_FALSE(parseSuiteLine("3r1k2/4npp1/1ppr3p/p6P/P2PPPP1/1NR5/5K2/2R5 w - - bm O-", p));
    REQUIRE(parseSuiteLine("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - bm O-O+; am 0-0-0!?", p));
    CHECK(p.best.size() == 1);
    CHECK(p.avoid.size() == 1);
}

TEST_CASE("EPD suite: a file, and its STS score") {
    const auto file = std::filesystem::temp_directory_path() / "fcg-tour-suite.epd";
    {
        std::ofstream out(file);
        out << "1k1r4/pp1b1R2/3q2pp/4p3/2B5/4Q3/PPP2B2/2K5 b - - bm Qd1+; c0 \"Qd1+=10, Qd2=3\";\n"
               "\n"
               "garbage\n"
               "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - bm e4 d4; c0 \"e4=10, d4=10, c4=6\";\n";
    }
    const auto suite = readSuite(file);
    REQUIRE(suite.positions.size() == 2);
    CHECK(suite.skipped == 1);
    CHECK(suite.positions[0].id == "#1");
    CHECK(suite.positions[1].id == "#2");
    CHECK(suite.hasPoints());
    CHECK(suite.maxPoints() == 20);
    CHECK_THROWS((void)readSuite(file.parent_path() / "fcg-tour-no-such-suite.epd"));
    std::filesystem::remove(file);
}

TEST_CASE("EPD suite: time to solution, the hold before stopping, and the final move") {
    SuitePosition p;
    REQUIRE(parseSuiteLine("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - bm e4; c0 \"e4=10, d4=7\";", p));

    SuiteSearch search(p, 2);
    CHECK_FALSE(search.info("info depth 1 time 10 score cp 20 pv d2d4", 10));
    CHECK_FALSE(search.info("info depth 2 time 30 score cp 25 pv e2e4 e7e5", 30));
    CHECK_FALSE(search.info("info depth 3 multipv 2 time 40 score cp 5 pv c2c4", 40));  // not the main line
    CHECK_FALSE(search.info("info depth 3 time 50 score cp 25 pv e2e4 e7e5", 50));
    CHECK(search.info("info depth 4 time 80 score cp 26 pv e2e4", 80));  // held 2 more plies: it may stop
    auto a = search.finish("e2e4", 85);
    CHECK(a.solved);
    CHECK(a.move == "e4");
    CHECK(a.time_ms == 30);
    CHECK(a.depth == 4);
    CHECK(a.points == 10);

    // Found, then left: the time counts from when it was found again.
    SuiteSearch back(p, 0);
    CHECK_FALSE(back.info("info depth 1 time 10 score cp 20 pv e2e4", 10));
    CHECK_FALSE(back.info("info depth 2 time 20 score cp 20 pv d2d4", 20));
    CHECK_FALSE(back.info("info depth 3 time 60 score cp 20 pv e2e4", 60));
    CHECK(back.finish("e2e4", 100).time_ms == 60);

    // Wrong in the end: its whole time, its points; an illegal move solves nothing.
    a = back.finish("d2d4", 100);
    CHECK_FALSE(a.solved);
    CHECK(a.move == "d4");
    CHECK(a.time_ms == 100);
    CHECK(a.points == 7);
    CHECK_FALSE(back.finish("e2e5", 100).solved);
    CHECK_FALSE(back.finish("", 100).solved);
}
