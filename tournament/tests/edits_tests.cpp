#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include <tour/journal.hpp>
#include <tour/standings.hpp>
#include <tour/state.hpp>
#include <tour/submission.hpp>
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

TEST_CASE("edit: a game of a replaced participant still being played is not recorded; its successor plays it") {
    auto state      = TournamentState::create(freshDir("replace-running"), roundRobin(3, 1));
    const auto task = state->claimNext();
    REQUIRE(task);
    const int old_id = task->encounter.first;

    Edit replace;
    replace.kind        = Edit::Kind::ReplaceParticipant;
    replace.participant = old_id;
    replace.engine      = engine("New");
    state->applyEdit(replace);

    CHECK_FALSE(state->complete(*task, data(*task, "1-0")).kept);
    play(*state, -1);
    CHECK(state->finished());

    const auto table = computeTable(state->spec(), state->results());
    std::map<std::string, int> games;
    for (const auto& row : table.rows) games[row.name] = row.games;
    CHECK(games["New"] == 4);
    CHECK(games[std::string(1, char('A' + old_id))] == 0);
    for (const auto& r : state->results()) {
        if (!r.voided) CHECK((r.white != old_id && r.black != old_id));
    }
}

TEST_CASE("state: the engines left are those with a game to play, of active participants") {
    auto spec   = roundRobin(4, 1);
    spec.format = Format::Gauntlet;
    spec.seeds  = 1;
    auto state  = TournamentState::create(freshDir("engines-left"), spec);
    CHECK(state->enginesLeft() == std::set<std::string>{"A", "B", "C", "D"});

    // The first opponent's two games: it has none left.
    const auto first = state->claimNext();
    REQUIRE(first);
    const int opponent = first->encounter.first == 0 ? first->encounter.second : first->encounter.first;
    state->complete(*first, data(*first, "1-0"));
    CHECK(state->enginesLeft().size() == 4u);  // the second game of the pair is still to play
    // Claimed, it is still to play: the runner thread that claimed it has not taken the engines yet, and a game
    // ending on another thread meanwhile must not have their idle processes ended.
    const auto second = state->claimNext();
    REQUIRE(second);
    CHECK(second->encounter.index == first->encounter.index);
    CHECK(state->enginesLeft().size() == 4u);
    state->complete(*second, data(*second, "1-0"));
    auto left = state->enginesLeft();
    CHECK(left.size() == 3u);
    CHECK_FALSE(left.count(std::string(1, char('A' + opponent))));

    Edit withdraw;
    withdraw.kind        = Edit::Kind::Withdraw;
    withdraw.participant = opponent == 3 ? 2 : 3;
    state->applyEdit(withdraw);
    left = state->enginesLeft();
    CHECK(left.size() == 2u);
    CHECK(left.count("A"));

    play(*state, -1);
    CHECK(state->finished());
    CHECK(state->enginesLeft().empty());
}

TEST_CASE("edit: a game of a withdrawn participant still being played is not recorded") {
    auto state      = TournamentState::create(freshDir("withdraw-running"), roundRobin(4, 1));
    const auto task = state->claimNext();
    REQUIRE(task);

    Edit withdraw;
    withdraw.kind        = Edit::Kind::Withdraw;
    withdraw.participant = task->encounter.first;
    state->applyEdit(withdraw);

    CHECK_FALSE(state->complete(*task, data(*task, "1-0")).kept);
    CHECK(state->results().empty());
}

TEST_CASE("edit: withdrawing a participant whose game was the last running finishes the tournament") {
    const auto dir = freshDir("withdraw-last");
    {
        auto state = TournamentState::create(dir, roundRobin(3, 1));
        CHECK(play(*state, 5) == 5);
        const auto task = state->claimNext();
        REQUIRE(task);
        CHECK_FALSE(state->claimNext());

        Edit withdraw;
        withdraw.kind        = Edit::Kind::Withdraw;
        withdraw.participant = task->encounter.first;
        state->applyEdit(withdraw);
        CHECK_FALSE(state->finished());  // its game is still being played

        CHECK_FALSE(state->complete(*task, data(*task, "1-0")).kept);
        CHECK(state->finished());
        CHECK(state->finishReason() == "complete");
    }
    CHECK(TournamentState::open(dir)->finished());
}

TEST_CASE("edit: withdrawing the participant of the last games left, none running, finishes the tournament") {
    auto state = TournamentState::create(freshDir("withdraw-idle"), roundRobin(3, 1));
    CHECK(play(*state, 4) == 4);
    // The last encounter has not started: its participants' games are no longer to play.
    const auto rows = state->scheduleRows();
    REQUIRE(rows.size() == 3);
    Edit withdraw;
    withdraw.kind        = Edit::Kind::Withdraw;
    withdraw.participant = rows[2].encounter.first;
    state->applyEdit(withdraw);
    CHECK(state->finished());
}

TEST_CASE("edit: the preview of a Swiss counts the rounds still to pair") {
    auto spec   = roundRobin(8, 1);
    spec.format = Format::Swiss;
    spec.rounds = 7;
    auto state  = TournamentState::create(freshDir("swiss-preview"), spec);
    play(*state, 2);  // the first encounter of round 1

    Edit replay;
    replay.kind       = Edit::Kind::VoidGames;
    replay.games      = {{0, 0}};
    const auto impact = state->preview(replay);
    CHECK(impact.error.empty());
    CHECK(impact.games_before == 56u);
    CHECK(impact.games_after == impact.games_before);
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

TEST_CASE("edit: the last change is undone until the next game starts, and stays undone after a reopen") {
    const auto dir = freshDir("undo");
    auto state     = TournamentState::create(dir, roundRobin(4, 1));
    play(*state, 3);
    const auto total = state->totalGames();

    // A game is running when an engine is withdrawn with its games voided.
    auto running = state->claimNext();
    REQUIRE(running);
    Edit withdraw;
    withdraw.kind        = Edit::Kind::Withdraw;
    withdraw.participant = 1;
    withdraw.void_played = true;
    state->applyEdit(withdraw);
    CHECK(state->totalGames() < total);
    CHECK(state->canUndo());

    // The running game finishes (it is kept), then the change is undone.
    state->complete(*running, data(*running, "1-0"));
    CHECK(state->canUndo());
    state->undoEdit();
    CHECK_FALSE(state->canUndo());
    CHECK(state->totalGames() == total);
    for (const auto& r : state->results()) CHECK_FALSE(r.voided);
    const auto played = state->results().size();

    // The journal says the same after a reopen.
    state.reset();
    state = TournamentState::open(dir);
    CHECK(state->totalGames() == total);
    CHECK(state->results().size() == played);
    for (const auto& r : state->results()) CHECK_FALSE(r.voided);
    CHECK(play(*state, -1) == static_cast<int>(*total - played));
    CHECK(state->finished());

    // Too late once a game has started under the change.
    Edit more;
    more.kind   = Edit::Kind::SetCycles;
    more.cycles = 2;
    state->applyEdit(more);
    CHECK_FALSE(state->finished());
    CHECK(state->canUndo());
    play(*state, 1);
    CHECK_FALSE(state->canUndo());
    CHECK_THROWS(state->undoEdit());
}

TEST_CASE("edit: undoing cycles added to a finished tournament finishes it again") {
    const auto dir = freshDir("undo-reopen");
    auto state     = TournamentState::create(dir, roundRobin(3, 1));
    play(*state, -1);
    REQUIRE(state->finished());

    Edit more;
    more.kind   = Edit::Kind::SetCycles;
    more.cycles = 2;
    state->applyEdit(more);
    CHECK_FALSE(state->finished());
    state->undoEdit();
    CHECK(state->finished());
    CHECK(state->totalGames() == 6u);

    state.reset();
    state = TournamentState::open(dir);
    CHECK(state->finished());
    CHECK(state->totalGames() == 6u);
    const auto summary = summarize(dir);
    REQUIRE(summary);
    CHECK(summary->finished);
    CHECK(summary->total == 6u);
}

TEST_CASE("programs: rebuilt, missing and new programs are noticed when the tournament is opened again") {
    const auto dir  = freshDir("programs");
    const auto bins = dir.parent_path() / "fcg-tour-edit-programs-bin";
    fs::remove_all(bins);
    fs::create_directories(bins);
    auto write = [&](const std::string& name, const std::string& contents) {
        std::ofstream(bins / name, std::ios::binary) << contents;
        return (bins / name).string();
    };

    auto spec = roundRobin(3, 1);
    for (auto& p : spec.participants) p.engine.cmd = write(p.engine.name, "build 1 of " + p.engine.name);
    auto state = TournamentState::create(dir, spec);
    CHECK(state->spec().participants[0].program.size > 0);

    Edit add;
    add.kind       = Edit::Kind::AddParticipant;
    add.engine     = engine("D");
    add.engine.cmd = write("D", "build 1 of D");
    add.elo        = 3100;
    state->applyEdit(add);
    CHECK(state->spec().participants.back().program.size > 0);
    state.reset();
    CHECK(TournamentState::open(dir)->warnings().empty());
    CHECK(TournamentState::open(dir)->spec().participants.back().elo == 3100);  // the newcomer's Elo, kept

    // Rebuilt later: same size, other contents (the date moves on, as it does; some file systems only keep seconds).
    const auto built = fs::last_write_time(bins / "A");
    write("A", "build 2 of A");
    fs::last_write_time(bins / "A", built + std::chrono::seconds(2));
    fs::remove(bins / "B");
    const auto warnings = TournamentState::open(dir)->warnings();
    REQUIRE(warnings.size() == 2);
    CHECK(warnings[0].find("program of A changed") != std::string::npos);
    CHECK(warnings[1].find("program of B is missing") != std::string::npos);
}

namespace {

Edit setResult(uint32_t encounter, int game, const std::string& result, const std::string& why = "") {
    Edit e;
    e.kind   = Edit::Kind::SetResult;
    e.games  = {{encounter, game}};
    e.result = result;
    e.why    = why;
    return e;
}

GameResult resultOf(const std::vector<GameResult>& results, uint64_t number) {
    const auto it = std::find_if(results.begin(), results.end(),
                                 [&](const GameResult& r) { return !r.voided && r.number() == number; });
    return it == results.end() ? GameResult{} : *it;
}

double pointsOf(const Table& table, int id) {
    for (const auto& row : table.rows) {
        if (row.id == id) return row.points;
    }
    return -1;
}

}  // namespace

TEST_CASE("edit: a result set by hand counts everywhere, the PGN file keeping the game as played") {
    const auto dir = freshDir("set-result");
    auto state     = TournamentState::create(dir, roundRobin(3, 1));
    play(*state, -1);  // every game a White win
    REQUIRE(state->finished());
    const auto game = resultOf(state->results(), 0);  // encounter 0, game 0
    const int white = game.white, black = game.black;
    const auto before = computeTable(state->spec(), state->results());

    // The impact: the points of the two players.
    const auto edit   = setResult(0, 0, "1/2-1/2", "a hang the program did not catch");
    const auto impact = state->preview(edit);
    CHECK(impact.error.empty());
    CHECK(impact.games_after == impact.games_before);
    CHECK(impact.replayed == 0);
    CHECK(impact.voided == 0);
    REQUIRE(impact.points.size() == 2);
    CHECK(impact.points.at(white) == std::pair{pointsOf(before, white), pointsOf(before, white) - 0.5});
    CHECK(impact.points.at(black) == std::pair{pointsOf(before, black), pointsOf(before, black) + 0.5});

    state->applyEdit(edit);
    CHECK(state->finished());  // nothing to play
    const auto set = resultOf(state->results(), 0);
    CHECK(set.result == "1/2-1/2");
    CHECK(set.termination == fastchess::MatchTermination::ADJUDICATION);
    CHECK(set.reason == OPERATOR_REASON);
    REQUIRE(set.set);
    CHECK(set.set->played_result == "1-0");
    CHECK(set.set->why == "a hang the program did not catch");

    // The standings follow; the statistics of the pair too.
    const auto after = computeTable(state->spec(), state->results());
    CHECK(pointsOf(after, white) == pointsOf(before, white) - 0.5);
    CHECK(pointsOf(after, black) == pointsOf(before, black) + 0.5);
    CHECK(state->pairStats(white, black).draws == 1);

    // The PGN file keeps the game as played; its text as it counts has the operator's result.
    const auto text = readSpan(state->pgnPath(), set.pgn);
    REQUIRE(text);
    CHECK(text->find("[Result \"1-0\"]") != std::string::npos);
    const auto counted = countedPgn(*text, set);
    CHECK(counted.find("[Result \"1/2-1/2\"]") != std::string::npos);
    CHECK(counted.find("[Result \"1-0\"]") == std::string::npos);
    CHECK(counted.find("[Termination \"adjudicated by the operator\"]") != std::string::npos);
    CHECK(counted.find("{Adjudicated by the operator: a hang the program did not catch (as played: 1-0") !=
          std::string::npos);
    const auto end = counted.find_last_not_of(" \n");
    REQUIRE(end != std::string::npos);
    REQUIRE(end >= 6);
    CHECK(counted.substr(end - 6, 7) == "1/2-1/2");

    // After a reopen, and for the rating list (a replay of the journal).
    state.reset();
    state = TournamentState::open(dir);
    CHECK(resultOf(state->results(), 0).result == "1/2-1/2");
    CHECK(pointsOf(computeTable(state->spec(), state->results()), white) == pointsOf(before, white) - 0.5);
    auto spec           = state->spec();
    const auto replayed = TournamentState::replay(Journal::read(dir / "journal.jsonl", false), spec);
    CHECK(resultOf(replayed.results, 0).result == "1/2-1/2");
}

TEST_CASE("edit: a result set by hand is undone, also after games were played; the played result takes it back") {
    const auto dir = freshDir("set-result-undo");
    auto state     = TournamentState::create(dir, roundRobin(4, 1));
    play(*state, 3);

    state->applyEdit(setResult(1, 0, "0-1"));
    CHECK(resultOf(state->results(), 2).result == "0-1");
    CHECK(state->undoLasts());
    play(*state, 2);  // a result set changes nothing the games depend on: undone exactly all the same
    CHECK(state->canUndo());
    state->undoEdit();
    CHECK_FALSE(state->canUndo());
    CHECK(resultOf(state->results(), 2).result == "1-0");
    CHECK_FALSE(resultOf(state->results(), 2).set);
    CHECK(state->results().size() == 5);

    state.reset();
    state = TournamentState::open(dir);
    CHECK(resultOf(state->results(), 2).result == "1-0");
    CHECK(state->results().size() == 5);

    // Set twice: the second reason replaces the first, the result played is kept; the played result takes it back.
    state->applyEdit(setResult(1, 0, "1/2-1/2", "first"));
    state->applyEdit(setResult(1, 0, "0-1", "second"));
    const auto twice = resultOf(state->results(), 2);
    CHECK(twice.result == "0-1");
    REQUIRE(twice.set);
    CHECK(twice.set->why == "second");
    CHECK(twice.set->played_result == "1-0");
    state->applyEdit(setResult(1, 0, "1-0"));
    CHECK_FALSE(resultOf(state->results(), 2).set);
    CHECK(resultOf(state->results(), 2).termination == fastchess::MatchTermination::NORMAL);
}

TEST_CASE("edit: a result is set on a game that counts, to another result") {
    auto state = TournamentState::create(freshDir("set-result-errors"), roundRobin(3, 1));
    play(*state, 2);
    CHECK_FALSE(state->preview(setResult(0, 0, "1-0")).error.empty());  // already its result
    CHECK_FALSE(state->preview(setResult(0, 0, "2-0")).error.empty());  // no result
    CHECK_FALSE(state->preview(setResult(2, 0, "0-1")).error.empty());  // not played
    CHECK_THROWS(state->applyEdit(setResult(2, 0, "0-1")));

    Edit voided;
    voided.kind  = Edit::Kind::VoidGames;
    voided.games = {{0, 1}};
    state->applyEdit(voided);
    CHECK_FALSE(state->preview(setResult(0, 1, "0-1")).error.empty());  // voided

    // A match takes it too.
    TournamentSpec spec;
    spec.format       = Format::Match;
    spec.max_games    = 4;
    spec.participants = {{0, engine("A")}, {1, engine("B")}};
    auto match        = TournamentState::create(freshDir("set-result-match"), spec);
    play(*match, 2);
    CHECK(match->preview(setResult(0, 1, "0-1")).error.empty());
    match->applyEdit(setResult(0, 1, "0-1"));
    CHECK(match->pairStats(0, 1).wins == 2);  // A has Black in game 1
}

TEST_CASE("edit: the export for a rating list gives the result set by hand") {
    const auto dir = freshDir("set-result-export");
    auto state     = TournamentState::create(dir, roundRobin(3, 1));
    play(*state, -1);
    state->applyEdit(setResult(0, 1, "1/2-1/2", "shuffling"));

    SubmissionExport e;
    e.tester       = "T";
    e.event        = "E";
    e.date         = "2026-11-14";
    const auto out = submissionGames(state->pgnPath(), state->results(), state->spec(), e);
    CHECK(out.games == 6);
    CHECK(out.pgn.find("[Termination \"adjudicated by the operator\"]") != std::string::npos);
    CHECK(out.pgn.find("{Adjudicated by the operator: shuffling") != std::string::npos);
    std::size_t draws = 0;
    for (auto at = out.pgn.find("[Result \"1/2-1/2\"]"); at != std::string::npos;
         at      = out.pgn.find("[Result \"1/2-1/2\"]", at + 1)) {
        ++draws;
    }
    CHECK(draws == 1);
    CHECK(submissionFacts(state->spec(), state->results(), state->totalGames()).played == 6);
}
