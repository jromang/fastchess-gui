// The SPRT of a match: an Elo bias on both bounds, and no decision before a number of games.

#include <doctest/doctest.hpp>

#include <filesystem>

#include <tour/sprt_rules.hpp>
#include <tour/state.hpp>

using namespace fcg::tour;
namespace fs = std::filesystem;

namespace {

TournamentSpec sprtMatch(double elo0, double elo1, double bias, uint64_t min_games) {
    TournamentSpec spec;
    spec.name   = "sprt";
    spec.format = Format::Match;
    for (int i = 0; i < 2; ++i) {
        Participant p;
        p.id          = i;
        p.engine.name = i == 0 ? "A" : "B";
        spec.participants.push_back(p);
    }
    const auto bounds            = biasedBounds(elo0, elo1, bias);
    spec.conditions.sprt.enabled = true;
    spec.conditions.sprt.elo0    = bounds.elo0;
    spec.conditions.sprt.elo1    = bounds.elo1;
    spec.conditions.sprt.alpha   = 0.05;
    spec.conditions.sprt.beta    = 0.05;
    spec.conditions.sprt.model   = "normalized";
    spec.sprt_bias               = bias;
    spec.sprt_min_games          = min_games;
    return spec;
}

// A wins the first game of each pair and draws the second: 1.5 - 0.5 every pair.
fastchess::MatchData winOfA(const GameTask& task) {
    const bool a_white = task.white.name == "A";
    const auto draw    = task.game == 1;
    fastchess::MatchData data(chess::constants::STARTPOS);
    data.players     = {fastchess::MatchData::PlayerInfo(task.white, draw      ? chess::GameResult::DRAW
                                                                     : a_white ? chess::GameResult::WIN
                                                                               : chess::GameResult::LOSE),
                        fastchess::MatchData::PlayerInfo(task.black, draw      ? chess::GameResult::DRAW
                                                                     : a_white ? chess::GameResult::LOSE
                                                                               : chess::GameResult::WIN)};
    data.termination = fastchess::MatchTermination::NORMAL;
    data.reason      = "test";
    data.duration    = "00:00:01";
    return data;
}

// Games A wins until the match ends, at most `limit`: how many were played.
uint64_t playUntilDecided(TournamentState& state, uint64_t limit) {
    uint64_t played = 0;
    while (!state.finished() && played < limit) {
        auto task = state.claimNext();
        REQUIRE(task);
        state.complete(*task, winOfA(*task));
        ++played;
    }
    return played;
}

}  // namespace

TEST_CASE("sprt: the bias moves both bounds, and comes off them again") {
    const auto b = biasedBounds(0, 3, 5);
    CHECK(b.elo0 == 5);
    CHECK(b.elo1 == 8);
    const auto n = biasedBounds(-5, 0, -2.5);
    CHECK(n.elo0 == -7.5);
    CHECK(n.elo1 == -2.5);
    const auto back = unbiasedBounds(b, 5);
    CHECK(back.elo0 == 0);
    CHECK(back.elo1 == 3);
    CHECK(biasedBounds(0, 3, 0).elo1 == 3);

    CHECK(sprtText(0, 3, 0, 0) == "SPRT [0, 3]");
    CHECK(sprtText(0, 3, 5, 0) == "SPRT [0, 3] + 5");
    CHECK(sprtText(-5, 0, -2.5, 2000) == "SPRT [-5, 0] - 2.5, no decision before 2000 games");
    CHECK(sprtText(0.5, 2.5, 0, 800) == "SPRT [0.5, 2.5], no decision before 800 games");
}

TEST_CASE("sprt: no decision before the minimum, however far the LLR; its verdict from then on") {
    const fastchess::SPRT sprt(0.05, 0.05, 0, 3, "logistic", true);
    const double upper = sprt.getUpperBound();
    const double lower = sprt.getLowerBound();
    CHECK(sprtDecision(sprt, upper + 1, 100, 0) == fastchess::SPRT_H1);
    CHECK(sprtDecision(sprt, upper + 1, 100, 2000) == fastchess::SPRT_CONTINUE);
    CHECK(sprtDecision(sprt, upper + 1, 1999, 2000) == fastchess::SPRT_CONTINUE);
    CHECK(sprtDecision(sprt, upper + 1, 2000, 2000) == fastchess::SPRT_H1);
    CHECK(sprtDecision(sprt, lower - 1, 2400, 2000) == fastchess::SPRT_H0);
    // Back between the bounds by the minimum: the test goes on.
    CHECK(sprtDecision(sprt, (upper + lower) / 2, 2400, 2000) == fastchess::SPRT_CONTINUE);
    // A test that is off decides nothing.
    const fastchess::SPRT off;
    CHECK(sprtDecision(off, 100, 5000, 0) == fastchess::SPRT_CONTINUE);
}

TEST_CASE("sprt: a match goes on to the minimum of games, the LLR beyond its bound, then stops with its verdict") {
    // A scoring 75 % crosses the upper bound after some pairs.
    {
        const auto dir = fs::temp_directory_path() / "fcg-sprt-quick";
        fs::remove_all(dir);
        auto state       = TournamentState::create(dir, sprtMatch(0, 5, 0, 0));
        const auto quick = playUntilDecided(*state, 5000);
        CHECK(state->finished());
        CHECK(state->finishReason() == "sprt_h1");
        REQUIRE(quick < 5000);

        // With a minimum of 20 games more: the same results, no decision before them.
        const auto minimum  = quick + 20;
        const auto slow_dir = fs::temp_directory_path() / "fcg-sprt-minimum";
        fs::remove_all(slow_dir);
        auto slow = TournamentState::create(slow_dir, sprtMatch(0, 5, 0, minimum));
        CHECK(playUntilDecided(*slow, quick + 2) == quick + 2);
        CHECK_FALSE(slow->finished());
        CHECK(playUntilDecided(*slow, 5000) == minimum - (quick + 2));
        CHECK(slow->finished());
        CHECK(slow->finishReason() == "sprt_h1");
        CHECK(slow->pairStats(0, 1).sum() == static_cast<int>(minimum));

        // Kept in tournament.json, and on reopening.
        slow.reset();
        const auto again = TournamentState::open(slow_dir, true);
        CHECK(again->spec().sprt_min_games == minimum);
        CHECK(again->finished());
    }
    // The bias: the bounds the SPRT was given are the tester's moved by it, kept in tournament.json.
    {
        const auto dir = fs::temp_directory_path() / "fcg-sprt-bias";
        fs::remove_all(dir);
        auto state = TournamentState::create(dir, sprtMatch(0, 3, 50, 0));
        CHECK(state->spec().conditions.sprt.elo0 == 50);
        CHECK(state->spec().conditions.sprt.elo1 == 53);
        CHECK(state->spec().sprt_bias == 50);
        nlohmann::json j = state->spec();
        CHECK(j.at("sprt_bias") == 50);
        auto plain = sprtMatch(0, 3, 0, 0);
        j          = plain;
        CHECK(j.count("sprt_bias") == 0);
        CHECK(j.count("sprt_min_games") == 0);
    }
}
