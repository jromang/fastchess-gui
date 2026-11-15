// Unbalanced openings: told from the evaluations of a finished game, and a pair played again from another opening.

#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <tour/by_opening.hpp>
#include <tour/state.hpp>
#include <tour/unbalanced.hpp>

using namespace fcg::tour;
namespace fs = std::filesystem;

namespace {

using fastchess::Score;
using fastchess::ScoreType;

Score cp(int64_t v) { return {ScoreType::CP, v}; }
Score mate(int64_t v) { return {ScoreType::MATE, v}; }

// White and Black in turn from White, each score from its mover's point of view; nullopt a move without one.
std::vector<PlyEval> plies(const std::vector<std::optional<Score>>& scores, int book = 0) {
    std::vector<PlyEval> out;
    bool white = true;
    for (int i = 0; i < book; ++i, white = !white) out.push_back({white, true, std::nullopt});
    for (const auto& s : scores) {
        out.push_back({white, false, s});
        white = !white;
    }
    return out;
}

// The positions of a small EPD book, each with the knights at home.
const char* const POSITIONS[] = {"rnbqkbnr/pppppppp/8/8/P7/8/1PPPPPPP/RNBQKBNR b KQkq - 0 1",
                                 "rnbqkbnr/pppppppp/8/8/1P6/8/P1PPPPPP/RNBQKBNR b KQkq - 0 1",
                                 "rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b KQkq - 0 1",
                                 "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1",
                                 "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1",
                                 "rnbqkbnr/pppppppp/8/8/7P/8/PPPPPPP1/RNBQKBNR b KQkq - 0 1"};

fs::path freshDir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("fcg-unbalanced-" + name);
    fs::remove_all(dir);
    return dir;
}

TournamentSpec bookMatch(const fs::path& dir, uint64_t games, bool replace) {
    const auto book = dir.parent_path() / (dir.filename().string() + ".epd");
    {
        std::ofstream out(book);
        for (const auto* fen : POSITIONS) out << fen << "\n";
    }
    TournamentSpec spec;
    spec.name                      = "unbalanced";
    spec.format                    = Format::Match;
    spec.max_games                 = games;
    spec.conditions.opening.file   = book.string();
    spec.conditions.opening.format = fastchess::FormatType::EPD;
    spec.conditions.opening.order  = fastchess::OrderType::SEQUENTIAL;
    spec.unbalanced_cp             = 200;
    spec.unbalanced_moves          = 2;
    spec.unbalanced_replace        = replace;
    for (int i = 0; i < 2; ++i) {
        Participant p;
        p.id          = i;
        p.engine.name = i == 0 ? "A" : "B";
        spec.participants.push_back(p);
    }
    return spec;
}

// A drawn game of four moves after a book move, the engines' scores from their own point of view: White's +300 and
// Black's -300 say White is far ahead.
fastchess::MatchData scoredGame(const GameTask& task, int64_t white_cp, int64_t black_cp) {
    fastchess::MatchData data(chess::constants::STARTPOS);
    data.players     = {fastchess::MatchData::PlayerInfo(task.white, chess::GameResult::DRAW),
                        fastchess::MatchData::PlayerInfo(task.black, chess::GameResult::DRAW)};
    data.termination = fastchess::MatchTermination::NORMAL;
    data.reason      = "test";
    data.duration    = "00:00:02";
    fastchess::MoveData book("b1c3");
    book.book = true;
    data.moves.push_back(book);
    const char* moves[] = {"g8f6", "g1f3", "f6g8", "f3g1"};
    for (int i = 0; i < 4; ++i) {
        const bool white = i % 2 == 1;  // the book move was White's
        data.moves.emplace_back(moves[i], cp(white ? white_cp : black_cp), 100, 10, 12, 1000);
    }
    return data;
}

// Plays the two games of the next pair.
std::vector<Recorded> playPair(TournamentState& state, int64_t white_cp, int64_t black_cp) {
    std::vector<Recorded> out;
    for (int g = 0; g < 2; ++g) {
        auto task = state.claimNext();
        REQUIRE(task);
        out.push_back(state.complete(*task, scoredGame(*task, white_cp, black_cp)));
    }
    return out;
}

}  // namespace

TEST_CASE("unbalanced opening: both engines beyond the score for their first moves after the book, for one side") {
    const UnbalancedRule rule{200, 2};
    // White's +250 and +300, Black's -220 and -400 (its own point of view): White ahead for both.
    CHECK(unbalancedOpening(plies({cp(250), cp(-220), cp(300), cp(-400)}), rule) == 1);
    // Black ahead.
    CHECK(unbalancedOpening(plies({cp(-250), cp(220), cp(-300), cp(400)}), rule) == -1);
    // One move under the score: not.
    CHECK(unbalancedOpening(plies({cp(250), cp(-150), cp(300), cp(-400)}), rule) == 0);
    // The engines disagree: not.
    CHECK(unbalancedOpening(plies({cp(250), cp(220), cp(300), cp(400)}), rule) == 0);
    // Exactly the score counts.
    CHECK(unbalancedOpening(plies({cp(200), cp(-200), cp(200), cp(-200)}), rule) == 1);
    // A mate is beyond any score; being mated, beyond any for the other side.
    CHECK(unbalancedOpening(plies({mate(5), cp(-900), cp(800), mate(-3)}), rule) == 1);
    // Only the first moves after the book count: what comes later does not.
    CHECK(unbalancedOpening(plies({cp(250), cp(-220), cp(300), cp(-400), cp(0), cp(0)}), rule) == 1);
    // The book moves are skipped, whoever played them.
    CHECK(unbalancedOpening(plies({cp(-250), cp(220), cp(-300), cp(400)}, 3), rule) == 1);  // Black moves first then
    // A move without a score among them, a game too short: nothing told.
    CHECK(unbalancedOpening(plies({cp(250), std::nullopt, cp(300), cp(-400)}), rule) == 0);
    CHECK(unbalancedOpening(plies({cp(250), cp(-220), cp(300)}), rule) == 0);
    CHECK(unbalancedOpening(plies({}), rule) == 0);
    // Off.
    CHECK(unbalancedOpening(plies({cp(250), cp(-220), cp(300), cp(-400)}), {0, 2}) == 0);
    CHECK(unbalancedOpening(plies({cp(250), cp(-220), cp(300), cp(-400)}), {200, 0}) == 0);

    // A participant's own book: its moves are book moves among the other engine's (one side only).
    std::vector<PlyEval> own{{true, true, std::nullopt}, {false, false, cp(-300)}, {true, true, std::nullopt},
                             {false, false, cp(-300)},   {true, false, cp(260)},   {true, false, cp(240)}};
    CHECK(unbalancedOpening(own, rule) == 1);

    // From a game fastchess played: Black to move first in its position, the book move marked.
    fastchess::MatchData data("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    fastchess::MoveData book("e7e5");
    book.book = true;
    data.moves.push_back(book);
    for (const auto& [move, score] :
         std::vector<std::pair<const char*, int64_t>>{{"g1f3", 300}, {"b8c6", -250}, {"f1b5", 320}, {"a7a6", -260}}) {
        data.moves.emplace_back(move, cp(score), 100, 10, 12, 1000);
    }
    CHECK(unbalancedOpening(data, rule) == 1);
    data.moves[2].score = cp(100);  // Black's first: not so far behind
    CHECK(unbalancedOpening(data, rule) == 0);
}

TEST_CASE("unbalanced opening: flagged when the game ends, in the journal, the results unchanged; by opening") {
    const auto dir    = freshDir("flag");
    auto state        = TournamentState::create(dir, bookMatch(dir, 4, false));
    const auto first  = playPair(*state, 300, -300);  // White far ahead in both games
    const auto second = playPair(*state, 30, -10);
    CHECK(first[0].result.unbalanced == 1);
    CHECK(first[1].result.unbalanced == 1);
    CHECK(first[1].pair.has_value());
    CHECK(second[0].result.unbalanced == 0);
    CHECK(state->openingsReplaced() == 0);
    CHECK(state->finished());
    for (const auto& r : state->results()) {
        CHECK_FALSE(r.voided);
        CHECK(r.result == "1/2-1/2");
    }

    // Kept on reopening, and counted by opening: the first pair's position is marked, the second's not.
    state.reset();
    const auto again   = TournamentState::open(dir, true);
    const auto results = again->results();
    REQUIRE(results.size() == 4);
    CHECK(results[0].unbalanced == 1);
    CHECK(results[3].unbalanced == 0);
    const auto openings = resultsByOpening(results, again->encounterOpenings(), false);
    REQUIRE(openings.size() == 2);
    CHECK(openings[0].unbalanced_white == 2);
    CHECK(openings[0].unbalancedGames() == 2);
    CHECK(openings[1].unbalancedGames() == 0);
    // Export as EPD…, without the unbalanced ones.
    std::vector<const OpeningStats*> shown{&openings[0], &openings[1]};
    CHECK(openingsEpd(shown) == openings[0].fen + "\n" + openings[1].fen + "\n");
    CHECK(openingsEpd(shown, true) == openings[1].fen + "\n");
}

TEST_CASE("unbalanced opening: the pair voided and played again from another opening, at most three times") {
    const auto dir = freshDir("replace");
    {
        auto state = TournamentState::create(dir, bookMatch(dir, 4, true));
        // The first pair, from the book's first position: White far ahead. Voided, its statistics not reported.
        const auto pair = playPair(*state, 300, -300);
        CHECK_FALSE(pair[1].pair.has_value());
        CHECK(state->openingsReplaced() == 1);
        const auto results = state->results();
        REQUIRE(results.size() == 2);
        for (const auto& r : results) {
            CHECK(r.voided);
            CHECK(r.void_reason == UNBALANCED_REASON);
        }
        CHECK(state->pairStats(0, 1).sum() == 0);
        CHECK(state->totalGames() == 4u);

        // Played again from the book's last position (in its playing order), the encounters to come keep theirs.
        auto task = state->claimNext();
        REQUIRE(task);
        CHECK(task->encounter.index == 0);
        CHECK(task->opening.fen_epd.rfind("rnbqkbnr/pppppppp/8/8/7P/", 0) == 0);
        state->release(*task);
    }

    // A resume plays the pair from that opening, and knows it was replaced.
    {
        auto state = TournamentState::open(dir);
        CHECK(state->openingsReplaced() == 1);
        CHECK(state->encounterOpenings().at(0).fen_epd.rfind("rnbqkbnr/pppppppp/8/8/7P/", 0) == 0);
        // Unbalanced again, twice more: replaced from the positions before the last.
        (void)playPair(*state, 300, -300);
        CHECK(state->openingsReplaced() == 2);
        CHECK(state->encounterOpenings().at(0).fen_epd.rfind("rnbqkbnr/pppppppp/8/8/4P3/", 0) == 0);
        (void)playPair(*state, -300, 300);  // Black ahead this time
        CHECK(state->openingsReplaced() == 3);
        // The fourth time, the pair counts, flagged.
        const auto kept = playPair(*state, 300, -300);
        CHECK(state->openingsReplaced() == 3);
        CHECK(kept[1].pair.has_value());
        CHECK(kept[1].result.unbalanced == 1);
        // The next pair, from the book's second position: the replacements did not move it.
        auto task = state->claimNext();
        REQUIRE(task);
        CHECK(task->encounter.index == 1);
        CHECK(task->opening.fen_epd.rfind("rnbqkbnr/pppppppp/8/8/1P6/", 0) == 0);
        state->complete(*task, scoredGame(*task, 0, 0));
        task = state->claimNext();
        REQUIRE(task);
        state->complete(*task, scoredGame(*task, 0, 0));
        CHECK(state->finished());
        CHECK(state->pairStats(0, 1).sum() == 4);
    }
}

TEST_CASE("unbalanced opening: the rule kept in tournament.json, nothing when it is off") {
    const auto dir   = freshDir("json");
    auto spec        = bookMatch(dir, 4, true);
    nlohmann::json j = spec;
    auto back        = j.get<TournamentSpec>();
    CHECK(back.unbalanced_cp == 200);
    CHECK(back.unbalanced_moves == 2);
    CHECK(back.unbalanced_replace);
    spec.unbalanced_cp = 0;
    j                  = spec;
    CHECK(j.count("unbalanced") == 0);
    back = j.get<TournamentSpec>();
    CHECK(back.unbalanced_cp == 0);
    CHECK_FALSE(back.unbalanced_replace);
}
