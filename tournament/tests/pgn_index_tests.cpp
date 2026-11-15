#include <doctest/doctest.hpp>

#include <sstream>

#include <tour/pgn_index.hpp>

using namespace fcg::tour;

TEST_CASE("pgn index: the games of a file, their places and tags") {
    const std::string text =
        "[Event \"Rapid \\\"A\\\"\"]\n"
        "[White \"Alpha 1.0\"]\r\n"
        "[Black \"Bravo\"]\n"
        "[Result \"1-0\"]\n"
        "[ECO \"B90\"]\n"
        "[PlyCount \"3\"]\n"
        "\n"
        "1. e4 {[%eval 0.3]} c5\n"
        "[%eval 0.2] 2. Nf3 1-0\n"
        "\n"
        "% an escape line\n"
        "[Event \"Rapid\"]\n"
        "[White \"Bravo\"]\n"
        "[Black \"Charlie\"]\n"
        "[Result \"1/2-1/2\"]\n"
        "[FEN \"8/8/8/8/8/8/8/K1k5 w - - 0 1\"]\n"
        "\n"
        "1. Kb1 1/2-1/2\n";
    std::istringstream in(text);
    std::atomic<uint64_t> done{0};
    const auto index = indexPgn(in, nullptr, &done);
    REQUIRE(index.size() == 2);
    CHECK(done == text.size());
    CHECK(index.bytes == text.size());

    const auto& a = index.games[0];
    CHECK(a.span.offset == 0);
    CHECK(index.str(a.event) == "Rapid \"A\"");
    CHECK(index.str(a.white) == "Alpha 1.0");
    CHECK(index.str(a.black) == "Bravo");
    CHECK(index.str(a.result) == "1-0");
    CHECK(index.str(a.eco) == "B90");
    CHECK(a.plies == 3);
    CHECK(a.fen == 0);
    // A comment's [%eval] at the start of a line is no tag: the game goes on to the next one's tags.
    const auto b_start = text.find("[Event \"Rapid\"]");
    CHECK(a.span.end() == b_start);

    const auto& b = index.games[1];
    CHECK(b.span.offset == b_start);
    CHECK(b.span.end() == text.size());
    CHECK(index.str(b.fen) == "8/8/8/8/8/8/8/K1k5 w - - 0 1");
    CHECK(b.white == a.black);  // kept once

    // The filter: every word, in any tag, letter case aside.
    CHECK(pgnGameMatches(index, a, ""));
    CHECK(pgnGameMatches(index, a, "alpha b90"));
    CHECK_FALSE(pgnGameMatches(index, a, "alpha charlie"));
    CHECK(pgnGameMatches(index, b, "CHARLIE 1/2"));
    CHECK(pgnGameMatches(index, b, "  bravo  "));
}

TEST_CASE("pgn index: moves without tags, text before the first game, a cancelled read") {
    {
        std::istringstream in("1. e4 e5 2. Nf3 *\n");
        const auto index = indexPgn(in);
        REQUIRE(index.size() == 1);
        CHECK(index.games[0].span.offset == 0);
        CHECK(index.games[0].span.length == 18);
    }
    {
        // No end of line at the end of the file.
        const std::string text = "some notes\n\n[White \"A\"]\n\n1. d4 *";
        std::istringstream in(text);
        const auto index = indexPgn(in);
        REQUIRE(index.size() == 2);
        CHECK(index.games[0].span.length == 12);
        CHECK(index.games[1].span.offset == 12);
        CHECK(index.games[1].span.end() == text.size());
    }
    {
        std::string many;
        for (int i = 0; i < 5000; ++i) many += "[White \"W" + std::to_string(i) + "\"]\n\n1. e4 *\n\n";
        std::istringstream in(many);
        std::atomic<bool> cancel{true};
        const auto index = indexPgn(in, &cancel);
        CHECK(index.size() < 5000);
        std::istringstream again(many);
        CHECK(indexPgn(again).size() == 5000);
    }
    {
        std::istringstream in("");
        CHECK(indexPgn(in).size() == 0);
    }
}
