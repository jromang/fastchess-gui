#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>
#include <map>

#include <chess.hpp>

#include <tour/polyglot.hpp>

#include "polyglot_writer.hpp"

using namespace fcg::tour;
namespace fs = std::filesystem;

namespace {

// A Polyglot move: to file, to rank, from file, from rank, 3 bits each, from "e2e4".
uint16_t move16(const std::string& uci) {
    const auto sq  = [](char f, char r) { return (r - '1') * 8 + (f - 'a'); };
    const int from = sq(uci[0], uci[1]), to = sq(uci[2], uci[3]);
    return static_cast<uint16_t>(((from / 8) << 9) | ((from % 8) << 6) | ((to / 8) << 3) | (to % 8));
}

void put(std::ofstream& out, uint64_t v, int bytes) {
    for (int i = bytes - 1; i >= 0; --i) out.put(static_cast<char>((v >> (8 * i)) & 0xff));
}

// A book from {moves leading to a position: [(move, weight)]}, entries unsorted on purpose.
fs::path writeBook(const std::string& name,
                   const std::map<std::string, std::vector<std::pair<std::string, int>>>& lines) {
    const auto path = fs::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (const auto& [line, moves] : lines) {
        chess::Board board;
        std::istringstream in(line);
        for (std::string uci; in >> uci;) board.makeMove(chess::uci::uciToMove(board, uci));
        for (const auto& [uci, weight] : moves) {
            put(out, board.hash(), 8);
            put(out, move16(uci), 2);
            put(out, static_cast<uint64_t>(weight), 2);
            put(out, 0, 4);
        }
    }
    return path;
}

}  // namespace

TEST_CASE("polyglot: the official test keys, from the program's boards and from the tests' own reckoning") {
    const std::pair<const char*, uint64_t> keys[] = {
        {"", 0x463b96181691fc9cULL},
        {"e2e4", 0x823c9b50fd114196ULL},
        {"e2e4 d7d5", 0x0756b94461c50fb0ULL},
        {"e2e4 d7d5 e4e5", 0x662fafb965db29d4ULL},
        {"e2e4 d7d5 e4e5 f7f5", 0x22a48b5a8e47ff78ULL},  // en passant: the e5 pawn may take on f6
        {"e2e4 d7d5 e4e5 f7f5 e1e2", 0x652a607ca3f242c1ULL},
        {"e2e4 d7d5 e4e5 f7f5 e1e2 e8f7", 0x00fdd303c946bdd9ULL},
        {"a2a4 b7b5 h2h4 b5b4 c2c4", 0x3c8123ea7b067637ULL},  // en passant: b4 may take on c3
        {"a2a4 b7b5 h2h4 b5b4 c2c4 b4c3 a1a3", 0x5c3f9b829b279560ULL},
    };
    for (const auto& [line, key] : keys) {
        INFO(line);
        chess::Board board;
        std::istringstream in(line);
        for (std::string uci; in >> uci;) playForBook(board, chess::uci::uciToMove(board, uci));
        CHECK(board.hash() == key);
        CHECK(fcg::test::polyglotKey(line) == key);
    }
}

TEST_CASE("polyglot: en passant is in the key whenever a pawn stands beside, even pinned, as the format wants") {
    // After c7c5, White's b5 pawn stands beside it but may not take: the black rook on h5 pins it to its king.
    const std::string fen = "8/2p5/8/KP5r/8/8/8/4k3 b - - 0 1";
    chess::Board board(fen);
    playForBook(board, chess::uci::uciToMove(board, "c7c5"));
    chess::Board legal_only(fen);
    legal_only.makeMove<true>(chess::uci::uciToMove(legal_only, "c7c5"));
    CHECK(board.hash() == fcg::test::polyglotKey("c7c5", fen));
    CHECK(legal_only.hash() != board.hash());
    // The same position without the double step (the pawn came from c6): no en passant in the key.
    CHECK(fcg::test::polyglotKey("c6c5", "8/8/2p5/KP5r/8/8/8/4k3 b - - 0 1") == legal_only.hash());
    // The capture is not generated all the same.
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    for (const auto& m : moves) CHECK(m.typeOf() != chess::Move::ENPASSANT);
}

TEST_CASE("polyglot: the lines are followed to the depth asked, rare moves left out, castling read") {
    const auto book = writeBook("fcg-polyglot-test.bin", {{"", {{"e2e4", 100}, {"d2d4", 50}, {"a2a3", 5}, {"h2h3", 0}}},
                                                          {"e2e4", {{"e7e5", 100}, {"c7c5", 80}}},
                                                          {"d2d4", {{"d7d5", 10}}},
                                                          {"e2e4 e7e5 g1f3 b8c6 f1c4 f8c5", {{"e1h1", 10}}},
                                                          {"e2e4 e7e5", {{"g1f3", 10}}},
                                                          {"e2e4 e7e5 g1f3", {{"b8c6", 10}}},
                                                          {"e2e4 e7e5 g1f3 b8c6", {{"f1c4", 10}}},
                                                          {"e2e4 e7e5 g1f3 b8c6 f1c4", {{"f8c5", 10}}}});

    // a2a3 weighs 5 % of e2e4: left out; h2h3 weighs nothing.
    auto positions = polyglotPositions(book, {1});
    CHECK(positions == std::vector<std::string>{"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq -",
                                                "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq -"});

    positions = polyglotPositions(book, {2});
    CHECK(positions.size() == 3);

    // Deep enough: the line ends with White castling short.
    positions = polyglotPositions(book, {20});
    CHECK(std::find(positions.begin(), positions.end(),
                    "r1bqk1nr/pppp1ppp/2n5/2b1p3/2B1P3/5N2/PPPP1PPP/RNBQ1RK1 b kq -") != positions.end());

    CHECK_THROWS_WITH((void)polyglotPositions(book, {20, 0.1, 2}), doctest::Contains("more than 2 positions"));
    CHECK_THROWS((void)polyglotPositions(fs::temp_directory_path() / "fcg-no-such-book.bin"));

    const auto epd = fs::temp_directory_path() / "fcg-polyglot-test.epd";
    CHECK(polyglotToEpd(book, epd, {2}) == 3);
    CHECK(fs::file_size(epd) > 0);
    CHECK(isPolyglot("Perfect2023.BIN"));
    CHECK_FALSE(isPolyglot("book.epd"));
}
