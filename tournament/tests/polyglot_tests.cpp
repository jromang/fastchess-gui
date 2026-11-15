#include <doctest/doctest.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

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

TEST_CASE("polyglot randomness: the weights as they are at 0, the best move only at -100, every move alike at 100") {
    const std::vector<uint16_t> w{100, 50, 25, 0};
    CHECK(choiceWeights(w, 0) == std::vector<uint64_t>{100, 50, 25, 0});
    CHECK(choiceWeights(w, 100) == std::vector<uint64_t>{1, 1, 1, 0});  // a move that weighs nothing stays out
    CHECK(choiceWeights(w, -100) == std::vector<uint64_t>{1, 0, 0, 0});
    CHECK(choiceWeights({50, 10, 50}, -100) == std::vector<uint64_t>{1, 0, 1});  // the best ones, tied
    CHECK(choiceWeights(w, 250) == choiceWeights(w, 100));                       // beyond the limits: clamped
    CHECK(choiceWeights(w, -250) == choiceWeights(w, -100));
    CHECK(choiceWeights({}, 30).empty());

    // 50: the square roots of the shares of the best; -50: their squares. Out of CHOICE_SCALE for the best.
    const auto half = choiceWeights(w, 50);
    CHECK(half[0] == CHOICE_SCALE);
    CHECK(half[1] == static_cast<uint64_t>(std::llround(std::sqrt(0.5) * CHOICE_SCALE)));
    CHECK(half[2] == CHOICE_SCALE / 2);
    CHECK(half[3] == 0);
    const auto squared = choiceWeights(w, -50);
    CHECK(squared == std::vector<uint64_t>{CHOICE_SCALE, CHOICE_SCALE / 4, CHOICE_SCALE / 16, 0});
    // From the best move only to every move alike, the second move's chance grows all the way.
    double previous = -1;
    for (int r = -100; r <= 100; r += 10) {
        const auto c       = choiceWeights({100, 50}, r);
        const double share = static_cast<double>(c[1]) / static_cast<double>(c[0] + c[1]);
        CHECK(share > previous);
        previous = share;
    }
    CHECK(previous == doctest::Approx(0.5));

    // The draw: each move in proportion to its weight, by the number given modulo the total.
    CHECK(drawChoice({3, 1}, 0) == std::optional<std::size_t>(0));
    CHECK(drawChoice({3, 1}, 2) == std::optional<std::size_t>(0));
    CHECK(drawChoice({3, 1}, 3) == std::optional<std::size_t>(1));
    CHECK(drawChoice({3, 1}, 4) == std::optional<std::size_t>(0));
    CHECK(drawChoice({0, 2}, 0) == std::optional<std::size_t>(1));
    CHECK_FALSE(drawChoice({0, 0}, 5));
    CHECK_FALSE(drawChoice({}, 5));

    CHECK(randomnessLabel(0) == "moves by weight");
    CHECK(randomnessLabel(-100) == "best moves only");
    CHECK(randomnessLabel(100) == "every move alike");
    CHECK(randomnessLabel(-40) == "randomness -40");
    CHECK(randomnessLabel(25) == "randomness +25");
}

TEST_CASE("polyglot randomness: the lines kept and their chances, from the best line only to every move alike") {
    const auto book   = writeBook("fcg-polyglot-randomness.bin", {{"", {{"e2e4", 100}, {"d2d4", 50}, {"a2a3", 5}}},
                                                                  {"e2e4", {{"e7e5", 100}, {"c7c5", 80}}},
                                                                  {"d2d4", {{"d7d5", 10}}}});
    const auto shares = [&](int randomness, int plies) {
        std::map<std::string, double> out;
        for (const auto& line : polyglotLines(book, {plies, 0.1, 200'000, randomness})) out[line.epd] = line.share;
        return out;
    };
    const std::string e4e5 = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -";
    const std::string e4c5 = "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq -";
    const std::string d4d5 = "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq -";

    // By the weights: a2a3 weighs less than a tenth of e2e4, left out; 1. e4 two times in three, then e5 or c5.
    auto s = shares(0, 2);
    REQUIRE(s.size() == 3);
    CHECK(s[e4e5] == doctest::Approx(2.0 / 3 * 100 / 180));
    CHECK(s[e4c5] == doctest::Approx(2.0 / 3 * 80 / 180));
    CHECK(s[d4d5] == doctest::Approx(1.0 / 3));
    double total = 0;
    for (const auto& [epd, share] : s) total += share;
    CHECK(total == doctest::Approx(1.0));
    // The same positions as before the randomness, in the same order.
    std::vector<std::string> order;
    for (const auto& line : polyglotLines(book, {2})) order.push_back(line.epd);
    CHECK(order == polyglotPositions(book, {2}));

    // The best move only: one line.
    s = shares(-100, 2);
    REQUIRE(s.size() == 1);
    CHECK(s[e4e5] == doctest::Approx(1.0));

    // Every move alike: a2a3 comes back, and each first move is as likely.
    s = shares(100, 1);
    REQUIRE(s.size() == 3);
    for (const auto& [epd, share] : s) CHECK(share == doctest::Approx(1.0 / 3));

    // Squared weights: d4 (a quarter of e4's) is kept, its share smaller than by the weights.
    s = shares(-50, 1);
    REQUIRE(s.size() == 2);
    CHECK(s["rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq -"] == doctest::Approx(0.25 / 1.25));
}

TEST_CASE("polyglot randomness: the openings drawn in order, each once, the likelier the sooner, the same seed alike") {
    const std::vector<PolyglotLine> lines{{"a", 0.9}, {"b", 0.05}, {"c", 0.05}};

    // Every position once, whatever the seed.
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        auto drawn = drawnInOrder(lines, seed);
        std::sort(drawn.begin(), drawn.end());
        CHECK(drawn == std::vector<std::string>{"a", "b", "c"});
    }
    // The same seed, the same order; seeds differ.
    CHECK(drawnInOrder(lines, 7) == drawnInOrder(lines, 7));
    std::set<std::vector<std::string>> orders;
    int a_first = 0;
    for (uint64_t seed = 1; seed <= 2000; ++seed) {
        const auto drawn = drawnInOrder(lines, seed);
        orders.insert(drawn);
        a_first += drawn.front() == "a";
    }
    CHECK(orders.size() > 1);
    // The first one drawn is "a" nine times in ten.
    CHECK(a_first > 1700);
    CHECK(a_first < 1900);

    // Written to an EPD file in that order.
    const auto book = writeBook("fcg-polyglot-drawn.bin", {{"", {{"e2e4", 100}, {"d2d4", 50}, {"c2c4", 20}}}});
    const auto epd  = fs::temp_directory_path() / "fcg-polyglot-drawn.epd";
    CHECK(polyglotToEpd(book, epd, {1}, 42) == 3);
    std::ifstream in(epd);
    std::vector<std::string> written;
    for (std::string line; std::getline(in, line);) written.push_back(line);
    CHECK(written == drawnInOrder(polyglotLines(book, {1}), 42));
}
