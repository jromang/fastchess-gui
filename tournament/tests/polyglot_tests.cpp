#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>
#include <map>

#include <chess.hpp>

#include <tour/polyglot.hpp>

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

TEST_CASE("polyglot: chess-library's hash is the Polyglot key (the reference values of the format)") {
    chess::Board board;
    CHECK(board.hash() == 0x463b96181691fc9cULL);
    const std::pair<const char*, uint64_t> steps[] = {{"e2e4", 0x823c9b50fd114196ULL}, {"d7d5", 0x0756b94461c50fb0ULL},
                                                      {"e4e5", 0x662fafb965db29d4ULL}, {"f7f5", 0x22a48b5a8e47ff78ULL},
                                                      {"e1e2", 0x652a607ca3f242c1ULL}, {"e8f7", 0x00fdd303c946bdd9ULL}};
    for (const auto& [uci, key] : steps) {
        board.makeMove(chess::uci::uciToMove(board, uci));
        CHECK(board.hash() == key);
    }
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
