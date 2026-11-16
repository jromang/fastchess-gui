// The variations of the Board page: moves played where the game went another way, the line shown, promoting and
// removing a variation, and variations written to and read from a PGN.

#include <doctest/doctest.hpp>

#include "board_page.hpp"
#include "board_text.hpp"

using namespace fcg;

namespace {

ReplayGame gameOf(const char* moves) {
    std::string error;
    auto text = readBoardText(moves);
    REQUIRE(text.kind == BoardText::Kind::Games);
    auto game = readPgnGame(text.pgn, error);
    REQUIRE(game);
    return *game;
}

chess::Move moveAt(const ReplayGame& game, int ply, const char* uci) {
    return chess::uci::uciToMove(boardAt(game, false, ply), uci);
}

// Their SAN, the annotations (!, ?) of a file left out.
std::vector<std::string> sans(const ReplayGame& game) {
    std::vector<std::string> out;
    for (const auto& m : game.moves) {
        auto san = m.san;
        while (!san.empty() && (san.back() == '!' || san.back() == '?')) san.pop_back();
        out.push_back(san);
    }
    return out;
}

}  // namespace

TEST_CASE("variations: a move where the game went another way starts one, the game kept") {
    GameTree tree{gameOf("e4 e5 Nf3 Nc6"), {}};
    // The game's own move: no variation.
    auto [line, ply] = playInTree(tree, false, -1, 2, moveAt(tree.main, 2, "g1f3"));
    CHECK(line == -1);
    CHECK(ply == 3);
    CHECK(tree.variations.empty());
    // Another one after 1.e4 e5: a variation from position 2.
    std::tie(line, ply) = playInTree(tree, false, -1, 2, moveAt(tree.main, 2, "f1c4"));
    CHECK(line == 0);
    CHECK(ply == 3);
    REQUIRE(tree.variations.size() == 1);
    CHECK(tree.variations[0].parent == -1);
    CHECK(tree.variations[0].from == 2);
    CHECK(sans(lineOf(tree, 0)) == std::vector<std::string>{"e4", "e5", "Bc4"});
    CHECK(sans(tree.main) == std::vector<std::string>{"e4", "e5", "Nf3", "Nc6"});
    // At its end the variation grows; the same first move again finds it.
    std::tie(line, ply) = playInTree(tree, false, 0, 3, moveAt(lineOf(tree, 0), 3, "g8f6"));
    CHECK(line == 0);
    CHECK(tree.variations[0].moves.size() == 2);
    std::tie(line, ply) = playInTree(tree, false, -1, 2, moveAt(tree.main, 2, "f1c4"));
    CHECK(line == 0);
    CHECK(tree.variations.size() == 1);
    // In the variation, at a position before it starts: the game's line holds that move, the new one branches
    // from the game.
    std::tie(line, ply) = playInTree(tree, false, 0, 1, moveAt(tree.main, 1, "c7c5"));
    CHECK(line == 1);
    CHECK(tree.variations[1].parent == -1);
    CHECK(tree.variations[1].from == 1);
    // At the end of the game, the game grows.
    std::tie(line, ply) = playInTree(tree, false, -1, 4, moveAt(tree.main, 4, "f1b5"));
    CHECK(line == -1);
    CHECK(tree.main.moves.size() == 5);
    // A variation in a variation.
    std::tie(line, ply) = playInTree(tree, false, 0, 3, moveAt(lineOf(tree, 0), 3, "f8c5"));
    CHECK(line == 2);
    CHECK(tree.variations[2].parent == 0);
    CHECK(tree.variations[2].from == 3);
    CHECK(ownerOf(tree, 2, 3) == 2);
    CHECK(ownerOf(tree, 2, 2) == 0);
    CHECK(ownerOf(tree, 2, 1) == -1);
    CHECK(branchPly(tree, 2, 4) == 2);
    CHECK(branchPly(tree, -1, 4) == 4);
}

TEST_CASE("variations: promoted, it becomes the line it branched from; removed, with those branching from it") {
    GameTree tree{gameOf("e4 e5 Nf3 Nc6 Bb5"), {}};
    playInTree(tree, false, -1, 2, moveAt(tree.main, 2, "f1c4"));  // 0: 2.Bc4
    auto v0 = lineOf(tree, 0);
    playInTree(tree, false, 0, 3, moveAt(v0, 3, "g8f6"));          // 2...Nf6 in it
    playInTree(tree, false, -1, 4, moveAt(tree.main, 4, "d2d4"));  // 1: 3.d4 instead of 3.Bb5
    v0 = lineOf(tree, 0);
    playInTree(tree, false, 0, 4, moveAt(v0, 4, "b1c3"));  // 3.Nc3 in the Bc4 line
    v0 = lineOf(tree, 0);
    playInTree(tree, false, 0, 4, moveAt(v0, 4, "d2d3"));  // 2: 3.d3 instead, in that line
    REQUIRE(tree.variations.size() == 3);

    const int shown = promoteVariation(tree, 0);
    CHECK(shown == -1);
    CHECK(sans(tree.main) == std::vector<std::string>{"e4", "e5", "Bc4", "Nf6", "Nc3"});
    CHECK(sans(lineOf(tree, 0)) == std::vector<std::string>{"e4", "e5", "Nf3", "Nc6", "Bb5"});
    // 3.d4 branched from the moves that left the game: it follows them; 3.d3 now branches from the game.
    CHECK(tree.variations[1].parent == 0);
    CHECK(tree.variations[2].parent == -1);
    CHECK(sans(lineOf(tree, 1)) == std::vector<std::string>{"e4", "e5", "Nf3", "Nc6", "d4"});
    CHECK(sans(lineOf(tree, 2)) == std::vector<std::string>{"e4", "e5", "Bc4", "Nf6", "d3"});

    const int parent = removeVariation(tree, 0);
    CHECK(parent == -1);
    REQUIRE(tree.variations.size() == 1);  // 3.d4 went with it
    CHECK(sans(lineOf(tree, 0)) == std::vector<std::string>{"e4", "e5", "Bc4", "Nf6", "d3"});
    CHECK(removeVariation(tree, 0) == -1);
    CHECK(tree.variations.empty());
}

TEST_CASE("variations: written in brackets after the move they replace, and read back") {
    GameTree tree{gameOf("e4 e5 Nf3 Nc6 Bb5 a6"), {}};
    playInTree(tree, false, -1, 2, moveAt(tree.main, 2, "f1c4"));
    playInTree(tree, false, 0, 3, moveAt(lineOf(tree, 0), 3, "g8f6"));
    playInTree(tree, false, -1, 1, moveAt(tree.main, 1, "c7c5"));
    playInTree(tree, false, 0, 3, moveAt(lineOf(tree, 0), 3, "f8c5"));
    const auto pgn = gamePgn(tree, false);
    CHECK(pgn.find("1. e4 e5 (1... c5) 2. Nf3 (2. Bc4 Nf6 (2... Bc5)) 2... Nc6 3. Bb5 a6") != std::string::npos);

    std::string error;
    const auto main = readPgnGame(pgn, error);
    REQUIRE(main);
    CHECK(main->moves.size() == 6);
    const auto read = readVariations(pgn, *main, false);
    INFO(pgn);
    REQUIRE(read.size() == 3);
    GameTree back{*main, read};
    // The same lines, whatever their order.
    std::vector<std::vector<std::string>> lines, expected;
    for (int v = 0; v < static_cast<int>(back.variations.size()); ++v) lines.push_back(sans(lineOf(back, v)));
    for (int v = 0; v < static_cast<int>(tree.variations.size()); ++v) expected.push_back(sans(lineOf(tree, v)));
    std::sort(lines.begin(), lines.end());
    std::sort(expected.begin(), expected.end());
    CHECK(lines == expected);
}

TEST_CASE("variations: those of another program's PGN, with comments, annotations and numbers") {
    const char* pgn = R"([Event "?"]
[White "A"]
[Black "B"]
[Result "*"]

1. e4 {the best} e5! (1... c5 $1 2. Nf3 (2. c3) d6 ; a comment
) (1... e6) 2. Nf3 Nc6?! (2... Nf6 3. Nxe5 (3. Bc4!? Nxe4)) 3. Bb5 *
)";
    std::string error;
    const auto main = readPgnGame(pgn, error);
    REQUIRE(main);
    CHECK(main->moves.size() == 5);
    const auto read = readVariations(pgn, *main, false);
    GameTree tree{*main, read};
    std::vector<std::vector<std::string>> lines;
    for (int v = 0; v < static_cast<int>(tree.variations.size()); ++v) lines.push_back(sans(lineOf(tree, v)));
    std::sort(lines.begin(), lines.end());
    const std::vector<std::vector<std::string>> expected = {{"e4", "c5", "Nf3", "d6"},
                                                            {"e4", "c5", "c3"},
                                                            {"e4", "e5", "Nf3", "Nf6", "Bc4", "Nxe4"},
                                                            {"e4", "e5", "Nf3", "Nf6", "Nxe5"},
                                                            {"e4", "e6"}};
    CHECK(lines == expected);
    // A move that cannot be played ends its variation, not the others.
    const char* broken = "[Event \"?\"]\n\n1. e4 e5 (1... Ke7 2. d4) (1... d5) 2. Nf3 *\n";
    const auto main2   = readPgnGame(broken, error);
    REQUIRE(main2);
    CHECK(readVariations(broken, *main2, false).size() == 1);
}

TEST_CASE("clocks: from [%clk] when the game has them, else rebuilt from the time control and the moves' times") {
    const auto game = parseReplay(R"([White "A"]
[Black "B"]
[TimeControl "10+0.1"]
[Result "*"]

1. e4 {book} e5 {book} 2. Nf3 {+0.30/12 0.500s} Nc6 {-0.25/11 1.250s} 3. Bb5 {+0.40/13 0.100s} *
)");
    REQUIRE(game);
    const auto clocks = moveClocks(*game);
    REQUIRE(clocks.size() == 5);
    CHECK(clocks[0] == 10000);  // a book move takes nothing
    CHECK(clocks[2] == 9600);   // 10 s − 0.5 s + 0.1 s
    CHECK(clocks[3] == 8850);
    CHECK(clocks[4] == 9600);  // 9.6 − 0.1 + 0.1

    const auto with_clk = parseReplay(R"([White "A"]
[Black "B"]
[Result "*"]

1. e4 {[%clk 0:03:00]} e5 {[%clk 0:02:58.5]} 2. Nf3 {[%clk 1:02:01]} *
)");
    REQUIRE(with_clk);
    const auto read = moveClocks(*with_clk);
    CHECK(read[0] == 180000);
    CHECK(read[1] == 178500);
    CHECK(read[2] == 3721000);

    // Neither: nothing.
    const auto none = parseReplay("[White \"A\"]\n[Black \"B\"]\n[Result \"*\"]\n\n1. e4 e5 *\n");
    REQUIRE(none);
    CHECK_FALSE(moveClocks(*none)[1].has_value());
}
