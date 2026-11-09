#include <doctest/doctest.hpp>

#include <filesystem>
#include <map>
#include <set>

#include <tour/standings.hpp>
#include <tour/state.hpp>
#include <tour/summary.hpp>

using namespace fcg::tour;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("fcg-tour-edit-" + name);
    fs::remove_all(dir);
    return dir;
}

fastchess::EngineConfiguration engine(const std::string& name) {
    fastchess::EngineConfiguration e;
    e.name = name;
    return e;
}

TournamentSpec roundRobin(int players, int cycles) {
    TournamentSpec spec;
    spec.format = Format::RoundRobin;
    spec.cycles = cycles;
    for (int i = 0; i < players; ++i) spec.participants.push_back({i, engine(std::string(1, char('A' + i)))});
    return spec;
}

fastchess::MatchData data(const GameTask& task, const std::string& result) {
    using chess::GameResult;
    fastchess::MatchData d(chess::constants::STARTPOS);
    const auto w  = result == "1-0" ? GameResult::WIN : result == "0-1" ? GameResult::LOSE : GameResult::DRAW;
    const auto b  = result == "1-0" ? GameResult::LOSE : result == "0-1" ? GameResult::WIN : GameResult::DRAW;
    d.players     = {fastchess::MatchData::PlayerInfo(task.white, w), fastchess::MatchData::PlayerInfo(task.black, b)};
    d.termination = fastchess::MatchTermination::NORMAL;
    d.duration    = "00:00:01";
    return d;
}

// Plays `n` games (all White wins), or everything when n < 0. Returns the games played.
int play(TournamentState& state, int n, const std::string& result = "1-0") {
    int played = 0;
    while (n < 0 || played < n) {
        const auto task = state.claimNext();
        if (!task) break;
        state.complete(*task, data(*task, result));
        ++played;
    }
    return played;
}

std::multiset<std::pair<int, int>> pairings(const TournamentState& state) {
    std::multiset<std::pair<int, int>> out;
    for (const auto& row : state.scheduleRows()) {
        out.insert(
            {std::min(row.encounter.first, row.encounter.second), std::max(row.encounter.first, row.encounter.second)});
    }
    return out;
}

}  // namespace

TEST_CASE("edit: a participant added with catch-up meets everyone in every cycle, nothing played moves") {
    const auto dir = freshDir("add");
    auto state     = TournamentState::create(dir, roundRobin(3, 2));
    CHECK(state->totalGames() == 12u);

    play(*state, 5);  // the first encounters of cycle 1
    const auto before = state->results();

    Edit add;
    add.kind     = Edit::Kind::AddParticipant;
    add.engine   = engine("D");
    add.catch_up = true;

    const auto impact = state->preview(add);
    CHECK(impact.error.empty());
    CHECK(impact.games_before == 12);
    CHECK(impact.games_after == 24);  // + 3 opponents x 2 cycles x 2 games
    CHECK(impact.replayed == 0);

    state->applyEdit(add);
    CHECK(state->totalGames() == 24u);
    CHECK(state->spec().participants.size() == 4);
    CHECK(state->spec().participants[3].id == 3);

    // Every pair of the four meets twice (one encounter per cycle).
    std::map<std::pair<int, int>, int> count;
    for (const auto& p : pairings(*state)) count[p]++;
    CHECK(count.size() == 6);
    for (const auto& [pair, n] : count) CHECK(n == 2);

    // Games already played keep their encounter numbers.
    const auto rows = state->scheduleRows();
    for (const auto& r : before) {
        CHECK(rows[r.encounter].status[r.game] == GameStatus::Done);
        CHECK(((rows[r.encounter].encounter.first == r.white && r.game == 0) ||
               (rows[r.encounter].encounter.second == r.white && r.game == 1)));
    }

    // The newcomer's catch-up comes right after what was started.
    bool d_soon = false;
    for (int i = 0; i < 4; ++i) {
        const auto task = state->claimNext();
        REQUIRE(task);
        d_soon |= task->white.name == "D" || task->black.name == "D";
        state->complete(*task, data(*task, "1/2-1/2"));
    }
    CHECK(d_soon);

    // Reopened, the journal gives the same tournament; the summary agrees.
    const auto rows_live = state->scheduleRows();
    state.reset();
    auto reopened = TournamentState::open(dir);
    CHECK(reopened->totalGames() == 24u);
    const auto rows_again = reopened->scheduleRows();
    REQUIRE(rows_again.size() == rows_live.size());
    for (std::size_t i = 0; i < rows_live.size(); ++i) {
        CHECK(rows_again[i].encounter.first == rows_live[i].encounter.first);
        CHECK(rows_again[i].encounter.second == rows_live[i].encounter.second);
    }
    const auto summary = summarize(dir);
    REQUIRE(summary);
    CHECK(summary->total == 24u);
    CHECK(summary->played == reopened->results().size());

    CHECK(play(*reopened, -1) == 24 - 9);
    CHECK(reopened->finished());
}

TEST_CASE("edit: without catch-up, the newcomer only plays the cycles not started") {
    auto state = TournamentState::create(freshDir("add-late"), roundRobin(3, 3));
    play(*state, 2);

    Edit add;
    add.kind     = Edit::Kind::AddParticipant;
    add.engine   = engine("D");
    add.catch_up = false;
    state->applyEdit(add);
    CHECK(state->totalGames() == 18u + 3u * 2u * 2u);  // cycles 2 and 3 only
}

TEST_CASE("edit: a replaced participant's games are played again by its successor") {
    auto state = TournamentState::create(freshDir("replace"), roundRobin(3, 1));
    play(*state, -1);
    CHECK(state->finished());

    Edit replace;
    replace.kind        = Edit::Kind::ReplaceParticipant;
    replace.participant = 1;
    replace.engine      = engine("B2");
    const auto impact   = state->preview(replace);
    CHECK(impact.error.empty());
    CHECK(impact.replayed == 4);  // B played 2 encounters, 4 games
    CHECK(impact.games_after == impact.games_before);

    state->applyEdit(replace);
    CHECK_FALSE(state->finished());  // reopened: games to play again
    CHECK(play(*state, -1) == 4);
    CHECK(state->finished());

    const auto table = computeTable(state->spec(), state->results());
    std::map<std::string, int> games;
    for (const auto& row : table.rows) games[row.name] = row.games;
    CHECK(games["B2"] == 4);
    CHECK(games["B"] == 0);  // voided, out of the standings
    CHECK(games["A"] == 4);

    int voided = 0;
    for (const auto& r : state->results()) voided += r.voided;
    CHECK(voided == 4);
}

TEST_CASE("edit: a withdrawn participant plays no more; its games can stop counting") {
    auto state = TournamentState::create(freshDir("withdraw"), roundRobin(4, 1));
    play(*state, 3);  // encounter 0 complete, encounter 1 half played

    Edit withdraw;
    withdraw.kind        = Edit::Kind::Withdraw;
    withdraw.participant = 0;
    withdraw.void_played = true;
    const auto impact    = state->preview(withdraw);
    CHECK(impact.error.empty());
    CHECK(impact.games_after < impact.games_before);

    state->applyEdit(withdraw);
    CHECK(state->totalGames() == impact.games_after);
    play(*state, -1);
    CHECK(state->finished());
    for (const auto& r : state->results()) {
        if (r.white == 0 || r.black == 0) CHECK(r.voided);
    }
    for (const auto& row : state->scheduleRows()) {
        if (row.encounter.first == 0 || row.encounter.second == 0) {
            CHECK(row.status[0] != GameStatus::Pending);
            CHECK(row.status[1] != GameStatus::Pending);
        }
    }
}

TEST_CASE("edit: cycles added to a finished tournament, cycles removed before they start") {
    auto state = TournamentState::create(freshDir("cycles"), roundRobin(3, 1));
    play(*state, -1);
    CHECK(state->finished());

    Edit more;
    more.kind   = Edit::Kind::SetCycles;
    more.cycles = 3;
    CHECK(state->preview(more).games_after == 18);
    state->applyEdit(more);
    CHECK_FALSE(state->finished());
    CHECK(state->totalGames() == 18u);

    play(*state, 2);
    Edit fewer;
    fewer.kind   = Edit::Kind::SetCycles;
    fewer.cycles = 2;
    state->applyEdit(fewer);
    CHECK(state->totalGames() == 12u);
    play(*state, -1);
    CHECK(state->finished());
    CHECK(state->results().size() == 12);
}

TEST_CASE("edit: voided games are played again, or dropped") {
    auto state = TournamentState::create(freshDir("void"), roundRobin(3, 1));
    play(*state, -1);

    Edit replay;
    replay.kind   = Edit::Kind::VoidGames;
    replay.games  = {{0, 1}};
    replay.why    = "crash";
    replay.replay = true;
    CHECK(state->preview(replay).replayed == 1);
    state->applyEdit(replay);
    CHECK_FALSE(state->finished());
    const auto task = state->claimNext();
    REQUIRE(task);
    CHECK(task->encounter.index == 0);
    CHECK(task->game == 1);
    state->complete(*task, data(*task, "0-1"));
    CHECK(state->finished());

    Edit drop;
    drop.kind         = Edit::Kind::VoidGames;
    drop.games        = {{1, 0}};
    drop.replay       = false;
    const auto impact = state->preview(drop);
    CHECK(impact.voided == 1);
    CHECK(impact.games_after == impact.games_before - 1);
    state->applyEdit(drop);
    CHECK(state->totalGames() == 5u);
    CHECK(state->finished());
}

TEST_CASE("edit: a match only accepts voided games") {
    TournamentSpec spec;
    spec.format       = Format::Match;
    spec.participants = {{0, engine("A")}, {1, engine("B")}};
    auto state        = TournamentState::create(freshDir("match"), spec);

    Edit add;
    add.kind   = Edit::Kind::AddParticipant;
    add.engine = engine("C");
    CHECK_FALSE(state->preview(add).error.empty());
    CHECK_THROWS(state->applyEdit(add));
}
