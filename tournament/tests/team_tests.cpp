#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include <tour/schedule.hpp>
#include <tour/standings.hpp>
#include <tour/state.hpp>
#include <tour/submission.hpp>
#include <tour/teams.hpp>

using namespace fcg::tour;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("fcg-tour-team-" + name);
    fs::remove_all(dir);
    return dir;
}

// Team A: the first `a` participants (A1, A2...), team B: the next `b` (B1...); ids are not indexes.
TournamentSpec teamSpec(int a, int b, bool boards, int cycles = 1) {
    TournamentSpec spec;
    spec.name        = "teams";
    spec.format      = Format::TeamMatch;
    spec.team_boards = boards;
    spec.cycles      = cycles;
    for (int i = 0; i < a + b; ++i) {
        Participant p;
        p.id          = 20 + i;
        p.team        = i < a ? 0 : 1;
        p.engine.name = i < a ? "A" + std::to_string(i + 1) : "B" + std::to_string(i - a + 1);
        spec.participants.push_back(p);
    }
    return spec;
}

fastchess::MatchData gameData(const GameTask& task, const std::string& result) {
    using chess::GameResult;
    fastchess::MatchData data(chess::constants::STARTPOS);
    const auto white = result == "1-0" ? GameResult::WIN : result == "0-1" ? GameResult::LOSE : GameResult::DRAW;
    const auto black = result == "1-0" ? GameResult::LOSE : result == "0-1" ? GameResult::WIN : GameResult::DRAW;
    data.players     = {fastchess::MatchData::PlayerInfo(task.white, white),
                        fastchess::MatchData::PlayerInfo(task.black, black)};
    data.termination = fastchess::MatchTermination::NORMAL;
    data.reason      = "test";
    data.duration    = "00:00:02";
    return data;
}

std::string readAll(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

GameResult game(uint32_t encounter, int g, int white, int black, const std::string& result) {
    GameResult r;
    r.encounter = encounter;
    r.game      = g;
    r.white     = white;
    r.black     = black;
    r.result    = result;
    return r;
}

}  // namespace

TEST_CASE("team rounds: every player against every player of the other team, once a cycle, once a round") {
    for (int a = 1; a <= 6; ++a) {
        for (int b = 1; b <= 6; ++b) {
            CAPTURE(a);
            CAPTURE(b);
            const auto rounds = teamRounds(a, b, false);
            CHECK(rounds.size() == static_cast<std::size_t>(std::max(a, b)));

            std::map<std::pair<int, int>, int> met;
            std::map<int, int> a_whites, b_whites;
            for (const auto& round : rounds) {
                CHECK(round.size() == static_cast<std::size_t>(std::min(a, b)));
                std::set<int> as, bs;
                for (const auto& p : round) {
                    CHECK(p.a >= 0);
                    CHECK(p.a < a);
                    CHECK(p.b >= 0);
                    CHECK(p.b < b);
                    CHECK(as.insert(p.a).second);  // nobody plays twice in a round
                    CHECK(bs.insert(p.b).second);
                    met[{p.a, p.b}]++;
                    (p.a_white ? a_whites[p.a] : b_whites[p.b])++;
                }
            }
            CHECK(met.size() == static_cast<std::size_t>(a * b));
            for (const auto& [pair, count] : met) CHECK(count == 1);
            // The colours of the first game are even for every player, give or take one.
            for (int i = 0; i < a; ++i) CHECK(std::abs(2 * a_whites[i] - b) <= 1);
            for (int j = 0; j < b; ++j) CHECK(std::abs(2 * b_whites[j] - a) <= 1);
        }
    }
}

TEST_CASE("team rounds: board by board, the k-th against the k-th, colours alternating from board to board") {
    const auto rounds = teamRounds(4, 4, true);
    REQUIRE(rounds.size() == 1);
    REQUIRE(rounds[0].size() == 4);
    for (int k = 0; k < 4; ++k) {
        CHECK(rounds[0][static_cast<std::size_t>(k)].a == k);
        CHECK(rounds[0][static_cast<std::size_t>(k)].b == k);
        CHECK(rounds[0][static_cast<std::size_t>(k)].a_white == (k % 2 == 0));
    }
    CHECK(teamRounds(0, 3, false).empty());
}

TEST_CASE("team match schedule: both pairing modes, cycles, colours reversed each cycle, no game within a team") {
    SUBCASE("every engine against every engine of the other team") {
        const auto spec = teamSpec(3, 2, false, 2);
        const Schedule schedule(spec);
        REQUIRE(schedule.size());
        CHECK(*schedule.size() == 3u * 2u * 2u);
        CHECK(schedule.editable());
        CHECK(schedule.lastCycle() == 2);
        CHECK(schedule.lastRound(1) == 3);

        std::map<std::pair<int, int>, int> as_first;
        for (uint32_t i = 0; i < *schedule.size(); ++i) {
            const auto e = *schedule.at(i);
            CHECK(e.index == i);
            const bool first_a = spec.participant(e.first)->team == 0, second_a = spec.participant(e.second)->team == 0;
            CHECK(first_a != second_a);
            as_first[{e.first, e.second}]++;
            // Cycle 2 is cycle 1 with the colours reversed.
            if (e.cycle == 2) {
                const auto mirror = *schedule.at(i - 6);
                CHECK(mirror.first == e.second);
                CHECK(mirror.second == e.first);
                CHECK(mirror.round == e.round);
            }
        }
        // Over two cycles every pair is met twice, each side first once.
        CHECK(as_first.size() == 12);
        for (const auto& [pair, count] : as_first) CHECK(count == 1);
    }
    SUBCASE("board by board") {
        const auto spec = teamSpec(3, 3, true, 3);
        const Schedule schedule(spec);
        REQUIRE(schedule.size());
        CHECK(*schedule.size() == 3u * 3u);
        for (uint32_t i = 0; i < *schedule.size(); ++i) {
            const auto e = *schedule.at(i);
            CHECK(e.cycle == i / 3 + 1);
            CHECK(e.round == 1);
            const int board = static_cast<int>(i % 3);
            const int a = 20 + board, b = 23 + board;
            // Team A has White first on boards 1 and 3 in odd cycles, on board 2 in even ones.
            const bool a_white = (board % 2 == 0) == (e.cycle % 2 == 1);
            CHECK(e.first == (a_white ? a : b));
            CHECK(e.second == (a_white ? b : a));
        }
    }
    SUBCASE("teams that do not fit") {
        CHECK_THROWS_AS(Schedule(teamSpec(3, 0, false)), std::invalid_argument);
        CHECK_THROWS_AS(Schedule(teamSpec(3, 2, true)), std::invalid_argument);
        CHECK_NOTHROW(Schedule(teamSpec(1, 1, true)));
    }
}

TEST_CASE("team match settings: version 4, the teams and their names kept") {
    auto spec          = teamSpec(2, 2, true);
    spec.team_names[0] = "Stockfish family";
    nlohmann::json j   = spec;
    CHECK(j.at("version") == 4);
    CHECK(j.at("format") == "team_match");
    const auto back = j.get<TournamentSpec>();
    CHECK(back.format == Format::TeamMatch);
    CHECK(back.team_boards);
    CHECK(back.teamName(0) == "Stockfish family");
    CHECK(back.teamName(1) == "Team B");
    CHECK(back.participants[2].team == 1);
    CHECK(back.participants[1].team == 0);
    CHECK(back.teamMembers(1) == std::vector<int>{22, 23});

    // Another format writes no team.
    auto rr   = spec;
    rr.format = Format::RoundRobin;
    for (auto& p : rr.participants) p.team = 0;
    const nlohmann::json other = rr;
    CHECK(other.at("version") == 1);
    CHECK_FALSE(other.contains("team_names"));
}

TEST_CASE("team standings: points, score, the Elo of the team's score, the crosstable of the two teams") {
    const auto spec = teamSpec(2, 2, false);
    // A1 (20) and A2 (21) against B1 (22) and B2 (23), encounters of two games, colours reversed.
    std::vector<GameResult> results = {
        game(0, 0, 20, 22, "1-0"),     game(0, 1, 22, 20, "0-1"),      // A1 wins both against B1
        game(1, 0, 23, 21, "1/2-1/2"), game(1, 1, 21, 23, "1/2-1/2"),  // A2 draws both with B2
        game(2, 0, 20, 23, "0-1"),     game(2, 1, 23, 20, "1/2-1/2"),  // A1 loses and draws against B2
        game(3, 0, 22, 21, "0-1"),                                     // A2 beats B1, the other game to come
    };
    results.push_back(game(3, 1, 21, 22, "0-1"));
    results.back().voided = true;  // voided: does not count

    const auto t = computeTeams(spec, results);
    CHECK(t.teams[0].name == "Team A");
    CHECK(t.teams[0].members.size() == 2);
    CHECK(t.teams[0].games == 7);
    CHECK(t.teams[1].games == 7);
    CHECK(t.teams[0].points == doctest::Approx(4.5));
    CHECK(t.teams[1].points == doctest::Approx(2.5));
    CHECK(t.teams[0].wins == 3);
    CHECK(t.teams[0].draws == 3);
    CHECK(t.teams[0].losses == 1);
    CHECK(t.teams[1].wins == 1);
    CHECK(t.teams[0].score() == doctest::Approx(4.5 / 7.0));

    // Pentanomial counts of the three complete encounters, from team A's side.
    CHECK(t.stats.totalPairs() == 3);
    CHECK(t.stats.penta_WW == 1);
    CHECK(t.stats.penta_DD == 1);
    CHECK(t.stats.penta_LD == 1);
    REQUIRE(t.elo);
    REQUIRE(t.error);
    CHECK(*t.elo > 0);
    CHECK(*t.error > 0);
    REQUIRE(t.los);
    CHECK(*t.los > 0.5);

    // The crosstable, each side's point of view.
    CHECK(t.pair(20, 22).points() == 2.0);
    CHECK(t.pair(22, 20).points() == 0.0);
    CHECK(t.pair(20, 23).points() == 0.5);
    CHECK(t.pair(23, 20).wins == 1);
    CHECK(t.pair(21, 22).games() == 1);
    CHECK(t.pair(21, 22).unpaired == 1);
    CHECK(t.teamOf(21) == 0);
    CHECK(t.teamOf(23) == 1);
    CHECK(t.teamOf(99) == -1);

    CHECK(teamScoreLine(t).rfind("Team A 4½ - 2½ Team B · 64.3 % · Elo +", 0) == 0);

    const auto engines = computeTable(spec, results);
    const auto text    = teamText(t, engines, "Teams", "7 games");
    CHECK(text.rfind("Teams\n\nTeam ", 0) == 0);
    CHECK(text.find("LOS of Team A") != std::string::npos);
    CHECK(text.find(" A1  A1 ") != std::string::npos);  // board A1, engine A1
    CHECK(text.find("    B1    B2\n") != std::string::npos);
    CHECK(text.find("    A1    A2\n") != std::string::npos);
    CHECK(text.substr(text.size() - 9) == "\n7 games\n");

    auto named          = spec;
    named.team_names[1] = "<b>";
    const auto html     = teamHtml(computeTeams(named, results), engines, "Teams");
    CHECK(html.find("&lt;b&gt;") != std::string::npos);
    CHECK(html.find("<b>") == std::string::npos);
    CHECK(html.find("<th title=\"B1\">B1</th>") != std::string::npos);

    // No game: no Elo.
    const auto none = computeTeams(spec, {});
    CHECK_FALSE(none.elo);
    CHECK(none.teams[0].score() == 0.0);
    CHECK(teamScoreLine(none) == "Team A 0 - 0 Team B");
}

TEST_CASE("team match: played, resumed exactly, the teams in the PGN tags, the standings from the results") {
    const auto dir  = freshDir("resume");
    auto spec       = teamSpec(2, 2, false, 2);
    spec.team_names = {"Old", "New"};

    std::vector<GameTask> claimed;
    {
        auto state = TournamentState::create(dir, spec);
        CHECK(state->totalGames() == 2u * 2u * 2u * 2u);
        for (int i = 0; i < 5; ++i) {
            auto task = state->claimNext();
            REQUIRE(task);
            claimed.push_back(*task);
        }
        // Four games recorded, the fifth running when the program stops.
        for (int i = 0; i < 4; ++i) state->complete(claimed[static_cast<std::size_t>(i)], gameData(claimed[i], "1-0"));
    }

    const auto pgn = readAll(dir / "teams.pgn");
    CHECK(pgn.find("[WhiteTeam \"Old\"]") != std::string::npos);
    CHECK(pgn.find("[BlackTeam \"New\"]") != std::string::npos);
    CHECK(pgn.find("[WhiteTeam \"New\"]") != std::string::npos);

    auto state = TournamentState::open(dir);
    CHECK(state->results().size() == 4);
    // The game that was running is played again first, with its opening, then the schedule goes on.
    auto next = state->claimNext();
    REQUIRE(next);
    CHECK(next->number() == claimed[4].number());
    CHECK(next->encounter.first == claimed[4].encounter.first);
    CHECK(next->white.name == claimed[4].white.name);
    CHECK(next->opening.fen_epd == claimed[4].opening.fen_epd);

    std::set<uint64_t> numbers;
    for (const auto& r : state->results()) numbers.insert(r.number());
    for (auto task = next; task; task = state->claimNext()) {
        CHECK(numbers.insert(task->number()).second);
        state->complete(*task, gameData(*task, "1/2-1/2"));
    }
    CHECK(numbers.size() == 16);
    CHECK(state->finished());

    // Every game is between the two teams; the standings add up.
    const auto t = computeTeams(state->spec(), state->results());
    CHECK(t.teams[0].games == 16);
    CHECK(t.teams[0].points + t.teams[1].points == doctest::Approx(16.0));
    CHECK(t.stats.totalPairs() == 8);
    CHECK(t.teams[0].name == "Old");

    // The export for a rating list keeps the team tags.
    SubmissionExport e;
    e.tester       = "T";
    e.event        = "E";
    e.date         = "2026-11-15";
    const auto out = submissionGames(state->pgnPath(), state->results(), state->spec(), e);
    CHECK(out.games == 16);
    CHECK(out.pgn.find("[WhiteTeam \"") != std::string::npos);
}

TEST_CASE("team match edits: a replacement takes the team and the board, a newcomer the team it is given") {
    SUBCASE("board by board") {
        auto state = TournamentState::create(freshDir("edit-boards"), teamSpec(2, 2, true, 2));
        // Board 1's first game played, then B2 (23) replaced.
        auto first = state->claimNext();
        REQUIRE(first);
        state->complete(*first, gameData(*first, "1-0"));

        Edit replace;
        replace.kind        = Edit::Kind::ReplaceParticipant;
        replace.participant = 23;
        replace.engine.name = "B2 new";
        REQUIRE(state->preview(replace).error.empty());
        state->applyEdit(replace);
        const auto* newcomer = state->spec().participant(24);
        REQUIRE(newcomer);
        CHECK(newcomer->team == 1);
        for (const auto& row : state->scheduleRows()) {
            CHECK(row.encounter.first != 23);
            CHECK(row.encounter.second != 23);
            // Board 2 is A2 (21) against the newcomer.
            if (row.encounter.first == 21 || row.encounter.second == 21) {
                CHECK((row.encounter.first == 24 || row.encounter.second == 24));
            }
        }

        // A newcomer would have no board.
        Edit add;
        add.kind        = Edit::Kind::AddParticipant;
        add.engine.name = "C";
        CHECK(state->preview(add).error.find("board by board") != std::string::npos);

        // More cycles repeat the boards, colours reversed every other cycle.
        Edit cycles;
        cycles.kind   = Edit::Kind::SetCycles;
        cycles.cycles = 3;
        REQUIRE(state->preview(cycles).error.empty());
        state->applyEdit(cycles);
        const auto rows = state->scheduleRows();
        REQUIRE(rows.size() == 6);
        CHECK(rows[4].encounter.cycle == 3);
        CHECK(rows[4].encounter.first == rows[0].encounter.first);
        CHECK(rows[5].encounter.first == rows[1].encounter.first);
        CHECK(rows[5].encounter.first == 24);  // board 2: B2's replacement has White first in odd cycles
        CHECK(rows[5].encounter.second == 21);
        CHECK(rows[3].encounter.first == 21);  // and A2 in even ones
    }
    SUBCASE("every engine against every engine of the other team") {
        auto state = TournamentState::create(freshDir("edit-all"), teamSpec(2, 2, false, 1));
        Edit add;
        add.kind        = Edit::Kind::AddParticipant;
        add.engine.name = "A3";
        add.team        = 0;
        REQUIRE(state->preview(add).error.empty());
        CHECK(state->preview(add).games_after == 8u + 4u);
        state->applyEdit(add);
        const auto* p = state->spec().participant(24);
        REQUIRE(p);
        CHECK(p->team == 0);
        int newcomer_games = 0;
        for (const auto& row : state->scheduleRows()) {
            const int other = row.encounter.first == 24    ? row.encounter.second
                              : row.encounter.second == 24 ? row.encounter.first
                                                           : -1;
            if (other < 0) continue;
            CHECK(state->spec().participant(other)->team == 1);
            ++newcomer_games;
        }
        CHECK(newcomer_games == 2);

        // Withdrawn: its games to come are dropped, the team keeps what it played.
        Edit withdraw;
        withdraw.kind        = Edit::Kind::Withdraw;
        withdraw.participant = 20;
        state->applyEdit(withdraw);
        for (const auto& row : state->scheduleRows()) {
            CHECK(row.encounter.first != 20);
            CHECK(row.encounter.second != 20);
        }
        CHECK(state->spec().teamMembers(0, true) == std::vector<int>{21, 24});
    }
}
