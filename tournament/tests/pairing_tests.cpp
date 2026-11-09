#include <doctest/doctest.hpp>

#include <filesystem>
#include <map>
#include <set>

#include <tour/pairing.hpp>
#include <tour/standings.hpp>
#include <tour/state.hpp>
#include <tour/summary.hpp>

using namespace fcg::tour;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("fcg-tour-pair-" + name);
    fs::remove_all(dir);
    return dir;
}

TournamentSpec field(Format format, int players) {
    TournamentSpec spec;
    spec.format = format;
    for (int i = 0; i < players; ++i) {
        Participant p;
        p.id          = i;
        p.engine.name = "P" + std::to_string(i);
        spec.participants.push_back(p);
    }
    return spec;
}

// The stronger participant (lower id) wins every game; equal results otherwise when `draws`.
fastchess::MatchData data(const GameTask& task, bool draws) {
    using chess::GameResult;
    fastchess::MatchData d(chess::constants::STARTPOS);
    const bool white_stronger = std::stoi(task.white.name.substr(1)) < std::stoi(task.black.name.substr(1));
    const auto w              = draws ? GameResult::DRAW : white_stronger ? GameResult::WIN : GameResult::LOSE;
    const auto b              = draws ? GameResult::DRAW : white_stronger ? GameResult::LOSE : GameResult::WIN;
    d.players     = {fastchess::MatchData::PlayerInfo(task.white, w), fastchess::MatchData::PlayerInfo(task.black, b)};
    d.termination = fastchess::MatchTermination::NORMAL;
    d.duration    = "00:00:01";
    return d;
}

int playAll(TournamentState& state, bool draws = false) {
    int n = 0;
    while (auto task = state.claimNext()) {
        state.complete(*task, data(*task, draws));
        REQUIRE(++n < 10000);
    }
    return n;
}

}  // namespace

TEST_CASE("pairing: bracket order and knockout byes") {
    CHECK(bracketOrder(8) == std::vector<int>{1, 8, 4, 5, 2, 7, 3, 6});
    CHECK(bracketOrder(2) == std::vector<int>{1, 2});

    const auto first = knockoutFirstRound({10, 11, 12, 13, 14, 15});
    CHECK(first.byes == std::vector<int>{10, 11});  // the two top seeds
    REQUIRE(first.pairs.size() == 2);
    REQUIRE(first.bracket.size() == 4);
    // Seeds 1 and 2 are in different halves.
    CHECK(first.bracket[0] == std::vector<int>{10});
    CHECK(first.bracket[2] == std::vector<int>{11});
}

TEST_CASE("pairing: Swiss round 1 is top half against bottom half; no rematch; byes rotate") {
    const std::vector<int> players = {0, 1, 2, 3, 4, 5, 6, 7};
    const auto r1                  = swissPairing(players, {}, {}, {});
    REQUIRE(r1.pairs.size() == 4);
    CHECK(r1.pairs[0] == std::make_pair(0, 4));
    CHECK(r1.pairs[3] == std::make_pair(3, 7));

    // After round 1 the winners meet each other, and nobody meets an opponent again.
    std::map<int, double> score          = {{0, 2}, {1, 2}, {2, 2}, {3, 2}};
    std::set<std::pair<int, int>> played = {{0, 4}, {1, 5}, {2, 6}, {3, 7}};
    const auto r2                        = swissPairing(players, score, played, {});
    for (const auto& [a, b] : r2.pairs) {
        CHECK_FALSE(played.count({std::min(a, b), std::max(a, b)}));
        CHECK(score[a] == score[b]);
    }

    const auto odd = swissPairing({0, 1, 2, 3, 4}, {}, {}, {4});
    REQUIRE(odd.byes.size() == 1);
    CHECK(odd.byes[0] == 3);  // the lowest ranked without a bye yet
}

TEST_CASE("Swiss: rounds paired as results come, no rematch, byes, resumable") {
    auto spec      = field(Format::Swiss, 7);
    spec.rounds    = 5;
    const auto dir = freshDir("swiss");
    auto state     = TournamentState::create(dir, spec);
    CHECK(state->totalGames() == 5u * 3u * 2u);

    // Half a tournament, then a reopen.
    for (int i = 0; i < 9; ++i) {
        auto task = state->claimNext();
        REQUIRE(task);
        state->complete(*task, data(*task, false));
    }
    state.reset();
    state = TournamentState::open(dir);
    playAll(*state);
    CHECK(state->finished());
    CHECK(state->results().size() == 30);

    std::set<std::pair<int, int>> met;
    for (const auto& row : state->scheduleRows()) {
        const auto key = std::make_pair(std::min(row.encounter.first, row.encounter.second),
                                        std::max(row.encounter.first, row.encounter.second));
        CHECK(met.insert(key).second);
        CHECK(row.encounter.round >= 1);
        CHECK(row.encounter.round <= 5);
    }

    const auto byes = state->byes();
    int total_byes  = 0;
    for (const auto& [id, n] : byes) {
        CHECK(n == 1);  // nobody gets two
        total_byes += n;
    }
    CHECK(total_byes == 5);

    // The strongest (P0, who wins every game) is first; a bye counts a point.
    const auto table = computeTable(state->spec(), state->results(), std::nullopt, byes);
    CHECK(table.rows[0].name == "P0");
    double points = 0;
    for (const auto& r : table.rows) points += r.points;
    CHECK(points == doctest::Approx(30 + 5));

    const auto summary = summarize(dir);
    REQUIRE(summary);
    CHECK(summary->finished);
    CHECK(summary->total == 30u);
}

TEST_CASE("Knockout: winners go through, the top seed wins, tied matches get tiebreaks") {
    auto spec   = field(Format::Knockout, 6);
    spec.cycles = 1;
    auto state  = TournamentState::create(freshDir("ko"), spec);

    playAll(*state);
    CHECK(state->finished());
    const auto brackets = state->brackets();
    REQUIRE(brackets.size() == 3);      // 6 players: 4 places, then 2, then the final
    CHECK(brackets.at(3).size() == 1);  // the final
    CHECK(brackets.at(3)[0] == std::vector<int>{0, 1});
    CHECK(state->results().size() == 5u * 2u);  // 5 matches of one pair of games

    // Every game drawn: each match goes to tiebreaks, then the higher seed after ten.
    auto drawn = TournamentState::create(freshDir("ko-draws"), field(Format::Knockout, 2));
    playAll(*drawn, true);
    CHECK(drawn->finished());
    CHECK(drawn->results().size() == 2u * 11u);  // the match and ten tiebreak pairs
}
