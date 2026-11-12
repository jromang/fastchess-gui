#include <doctest/doctest.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#ifndef _WIN32
#    include <sys/wait.h>
#    include <unistd.h>
#endif

#include <tour/engine_text.hpp>
#include <tour/files.hpp>
#include <tour/journal.hpp>
#include <tour/state.hpp>
#include <tour/summary.hpp>

using namespace fcg::tour;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("fcg-tour-" + name);
    fs::remove_all(dir);
    return dir;
}

TournamentSpec matchSpec(uint64_t max_games = 0) {
    TournamentSpec spec;
    spec.name      = "test";
    spec.format    = Format::Match;
    spec.max_games = max_games;
    for (int i = 0; i < 2; ++i) {
        Participant p;
        p.id          = i;
        p.engine.name = i == 0 ? "A" : "B";
        spec.participants.push_back(p);
    }
    return spec;
}

// A finished game as fastchess would report it, without moves.
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

}  // namespace

TEST_CASE("journal: a last line cut short is dropped and removed; a bad line is an error") {
    const auto dir = freshDir("journal");
    fs::create_directories(dir);
    const auto file = dir / "journal.jsonl";

    {
        Journal journal(file);
        journal.append({{"t", "a"}});
        journal.append({{"t", "b"}});
    }
    {
        std::ofstream out(file, std::ios::binary | std::ios::app);
        out << R"({"t":"c","cut)";
    }

    const auto events = Journal::read(file);
    REQUIRE(events.size() == 2);
    CHECK(events[1]["t"] == "b");
    CHECK(readAll(file).back() == '\n');

    {
        std::ofstream out(file, std::ios::binary | std::ios::app);
        out << "not json\n";
    }
    CHECK_THROWS_AS((void)Journal::read(file), std::runtime_error);

    // Text that is not UTF-8 (an engine's) is journaled with U+FFFD, not refused.
    const auto other = dir / "other.jsonl";
    {
        Journal journal(other);
        journal.append({{"t", "a"}, {"reason", "G\xE9za"}});
    }
    const auto latin = Journal::read(other);
    REQUIRE(latin.size() == 1);
    CHECK(latin[0]["reason"] == "G\xEF\xBF\xBDza");
}

TEST_CASE("state: pairs are played back to back, results survive a reopen, PGN orphans are cut") {
    const auto dir = freshDir("state");

    std::vector<GameTask> claimed;
    {
        auto state = TournamentState::create(dir, matchSpec(10));
        CHECK(state->totalGames() == 10u);

        // Game 0 then game 1 of the same encounter, colours swapped.
        for (int i = 0; i < 4; ++i) {
            auto task = state->claimNext();
            REQUIRE(task);
            claimed.push_back(*task);
        }
        CHECK(claimed[0].encounter.index == 0);
        CHECK(claimed[0].game == 0);
        CHECK(claimed[1].encounter.index == 0);
        CHECK(claimed[1].game == 1);
        CHECK(claimed[0].white.name == "A");
        CHECK(claimed[1].white.name == "B");
        CHECK(claimed[2].encounter.index == 1);

        // Encounter 0 complete (A wins both), encounter 1 half played, game 3 still "running" at the crash.
        const auto r0 = state->complete(claimed[0], gameData(claimed[0], "1-0"));
        CHECK_FALSE(r0.pair);
        const auto r1 = state->complete(claimed[1], gameData(claimed[1], "0-1"));
        REQUIRE(r1.pair);
        CHECK(r1.pair->wins == 2);
        CHECK(r1.pair->penta_WW == 1);
        state->complete(claimed[2], gameData(claimed[2], "1/2-1/2"));

        const auto stats = state->pairStats(0, 1);
        CHECK(stats.wins == 2);
        CHECK(stats.sum() == 2);  // only complete encounters count
        CHECK(state->pairStats(1, 0).losses == 2);
    }

    // A game written to the PGN files but never journaled (crash between the two writes).
    const auto pgn_before = fs::file_size(dir / "games.pgn");
    {
        std::ofstream out(dir / "games.pgn", std::ios::binary | std::ios::app);
        out << "[Event \"orphan\"]\n\n1. e4 *\n\n";
    }

    auto state = TournamentState::open(dir);
    CHECK(fs::file_size(dir / "games.pgn") == pgn_before);
    CHECK(state->results().size() == 3);
    CHECK_FALSE(state->finished());

    // The interrupted game comes first, with the opening its encounter started with.
    auto next = state->claimNext();
    REQUIRE(next);
    CHECK(next->encounter.index == 1);
    CHECK(next->game == 1);
    CHECK(next->opening.fen_epd == claimed[3].opening.fen_epd);

    // Playing everything that is left finishes the match with exactly 10 games, each once.
    std::set<uint64_t> numbers;
    for (const auto& r : state->results()) numbers.insert(r.number());
    for (auto task = next; task; task = state->claimNext()) {
        const auto rec = state->complete(*task, gameData(*task, "1/2-1/2"));
        CHECK(numbers.insert(rec.result.number()).second);
    }
    CHECK(numbers.size() == 10);
    CHECK(state->finished());
    CHECK(state->finishReason() == "complete");
    CHECK_FALSE(state->hasWork());

    // Every game is in the PGN, once.
    const auto text  = readAll(dir / "games.pgn");
    std::size_t hits = 0;
    for (auto pos = text.find("[Event "); pos != std::string::npos; pos = text.find("[Event ", pos + 1)) ++hits;
    CHECK(hits == 10);

    // And a reopened finished tournament has nothing to play.
    state.reset();
    auto again = TournamentState::open(dir);
    CHECK(again->finished());
    CHECK_FALSE(again->claimNext());
}

TEST_CASE("state: a released game is played again first") {
    auto state   = TournamentState::create(freshDir("release"), matchSpec());
    const auto a = state->claimNext();
    const auto b = state->claimNext();
    REQUIRE(a);
    REQUIRE(b);
    const auto claimed = state->version();
    state->release(*a);
    CHECK(state->version() > claimed);  // the views see the change
    const auto c = state->claimNext();
    REQUIRE(c);
    CHECK(c->number() == a->number());
    const auto before = state->version();
    state->complete(*c, gameData(*c, "1-0"));
    CHECK(state->version() > before);
}

TEST_CASE("summaries: kept between two listings, read again when the journal changed") {
    const auto root = freshDir("summaries");
    fs::create_directories(root);
    auto state = TournamentState::create(root / "20260101-000000 test", matchSpec());

    SummaryCache cache;
    auto list = cache.list(root);
    REQUIRE(list.size() == 1);
    CHECK(list[0].played == 0);
    CHECK(cache.list(root).size() == 1);

    const auto task = state->claimNext();
    REQUIRE(task);
    state->complete(*task, gameData(*task, "1-0"));
    list = cache.list(root);
    REQUIRE(list.size() == 1);
    CHECK(list[0].played == 1);

    state.reset();
    fs::remove_all(root / "20260101-000000 test");
    CHECK(cache.list(root).empty());
}

TEST_CASE("state: the SPRT ends a match and the verdict is journaled") {
    auto spec                    = matchSpec();
    spec.conditions.sprt.enabled = true;
    spec.conditions.sprt.elo0    = 0;
    spec.conditions.sprt.elo1    = 5;
    spec.conditions.sprt.alpha   = 0.05;
    spec.conditions.sprt.beta    = 0.05;
    spec.conditions.sprt.model   = "normalized";
    const auto dir               = freshDir("sprt");

    {
        auto state = TournamentState::create(dir, spec);
        int played = 0;
        // A wins every pair 1.5 - 0.5: H1 is accepted quickly.
        while (auto task = state->claimNext()) {
            const bool a_white = task->white.name == "A";
            const auto result  = task->game == 0 ? (a_white ? "1-0" : "0-1") : "1/2-1/2";
            state->complete(*task, gameData(*task, result));
            REQUIRE(++played < 5000);
        }
        CHECK(state->finished());
        CHECK(state->finishReason() == "sprt_h1");
    }

    auto reopened = TournamentState::open(dir);
    CHECK(reopened->finished());
    CHECK(reopened->finishReason() == "sprt_h1");
}

TEST_CASE("state: a malformed journal is reported with its position") {
    const auto dir = freshDir("bad-journal");
    {
        auto state = TournamentState::create(dir, matchSpec());
    }
    {
        std::ofstream out(dir / "journal.jsonl", std::ios::binary | std::ios::app);
        out << R"({"t":"teleport"})" << "\n";
    }
    CHECK_THROWS_WITH_AS((void)TournamentState::open(dir), doctest::Contains("event 1"), std::runtime_error);
}

TEST_CASE("journal: a damaged line is skipped and reported; a partial line left in place gets its own line") {
    const auto dir = freshDir("journal-damaged");
    fs::create_directories(dir);
    const auto file = dir / "journal.jsonl";
    {
        Journal journal(file);
        journal.append({{"t", "a"}});
    }
    {
        std::ofstream out(file, std::ios::binary | std::ios::app);
        out << std::string(5, '\0') << "\n" << R"({"t":"b","cut)";
    }
    // Opened without a repair first: the next line must not be glued to the partial one.
    {
        Journal journal(file);
        journal.append({{"t", "c"}});
    }

    std::vector<std::string> damaged;
    const auto events = Journal::read(file, false, &damaged);
    REQUIRE(events.size() == 2);
    CHECK(events[0]["t"] == "a");
    CHECK(events[1]["t"] == "c");
    REQUIRE(damaged.size() == 2);
    CHECK(damaged[0].find("line 2") != std::string::npos);
    CHECK(damaged[1].find("line 3") != std::string::npos);
}

TEST_CASE("state: a damaged journal line is skipped with a warning, the tournament still opens") {
    const auto dir = freshDir("state-damaged");
    {
        auto state      = TournamentState::create(dir, matchSpec(4));
        const auto task = state->claimNext();
        state->complete(*task, gameData(*task, "1-0"));
    }
    {
        std::ofstream out(dir / "journal.jsonl", std::ios::binary | std::ios::app);
        out << "{\"t\":\"game_fin\x01\x02\n";
    }
    auto state = TournamentState::open(dir);
    CHECK(state->results().size() == 1);
    REQUIRE(state->warnings().size() == 1);
    CHECK(state->warnings()[0].find("skipped") != std::string::npos);
}

TEST_CASE("state: a PGN file that lost its end is filled up, new games do not land on journaled ones") {
    const auto dir = freshDir("state-pgn-lost");
    uint64_t end   = 0;
    {
        auto state = TournamentState::create(dir, matchSpec(4));
        for (int i = 0; i < 2; ++i) {
            const auto task = state->claimNext();
            state->complete(*task, gameData(*task, "1-0"));
        }
        end = state->results().back().pgn.end();
    }
    // The disk kept the journal but not the end of the PGN (a power cut on a disk that reorders writes).
    fs::resize_file(dir / "games.pgn", end - 10);

    auto state = TournamentState::open(dir);
    REQUIRE(state->warnings().size() == 1);
    CHECK(state->warnings()[0].find("10 bytes") != std::string::npos);
    const auto task = state->claimNext();
    const auto rec  = state->complete(*task, gameData(*task, "1-0"));
    CHECK(rec.result.pgn.offset == end);
    CHECK(readSpan(dir / "games.pgn", rec.result.pgn)->find("[Event ") == 0);
}

TEST_CASE("state: one program at a time writes a tournament folder; others can read it") {
    const auto dir = freshDir("state-lock");
    auto state     = TournamentState::create(dir, matchSpec(4));
    {
        const auto task = state->claimNext();
        state->complete(*task, gameData(*task, "1-0"));
    }

    // The same program may open it again (the window resumes the tournament it shows).
    CHECK_NOTHROW((void)TournamentState::open(dir));

    // Read-only: nothing to claim, no edit, the files untouched.
    const auto journal = readAll(dir / "journal.jsonl");
    auto shown         = TournamentState::open(dir, true);
    CHECK(shown->readOnly());
    CHECK(shown->results().size() == 1);
    CHECK_FALSE(shown->claimNext());
    Edit edit;
    edit.kind  = Edit::Kind::VoidGames;
    edit.games = {{0, 0}};
    CHECK_THROWS_AS(shown->applyEdit(edit), std::runtime_error);
    CHECK(readAll(dir / "journal.jsonl") == journal);

#ifndef _WIN32
    // Another program is refused while this one has it open.
    const auto refused = [&] {
        const pid_t child = fork();
        if (child == 0) {
            int code = 3;
            try {
                (void)TournamentState::open(dir);
                code = 0;
            } catch (const FolderBusy&) {
                code = 1;
            } catch (...) {
                code = 2;
            }
            _exit(code);
        }
        int status = 0;
        waitpid(child, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    };
    CHECK(refused() == 1);
    shown.reset();
    state.reset();
    CHECK(refused() == 0);
#endif
}

TEST_CASE("files: a replaced file keeps its permissions, and the old content as .bak when asked") {
    const auto dir = freshDir("files");
    fs::create_directories(dir);
    const auto file = dir / "settings.json";
    replaceFile(file, "one");
    CHECK(readAll(file) == "one");
    CHECK_FALSE(fs::exists(fs::path(file).concat(".bak")));
#ifndef _WIN32
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
#endif
    replaceFile(file, "two", true);
    CHECK(readAll(file) == "two");
    CHECK(readAll(fs::path(file).concat(".bak")) == "one");
    CHECK_FALSE(fs::exists(fs::path(file).concat(".tmp")));
#ifndef _WIN32
    CHECK(fs::status(file).permissions() == (fs::perms::owner_read | fs::perms::owner_write));
#endif
}

TEST_CASE("state: the PV warnings of a game are kept with its result, by ply") {
    const auto dir = freshDir("state-pv");
    {
        auto state = TournamentState::create(dir, matchSpec(2));
        auto task  = state->claimNext();
        REQUIRE(task);
        auto data = gameData(*task, "1-0");
        for (const auto* move : {"e2e4", "e7e5", "g1f3"}) data.moves.emplace_back(move);
        data.moves[1].pv_warnings = {"Warning; Illegal PV move - move e5e4 from B"};
        data.moves[2].pv_warnings = {"Warning; Incomplete mating PV - from A", "Warning; Too long mating PV - from A"};
        const auto recorded       = state->complete(*task, data);
        CHECK(recorded.result.pv_warnings.size() == 3);
    }
    const auto results = TournamentState::open(dir)->results();
    REQUIRE(results.size() == 1);
    const auto& pv = results[0].pv_warnings;
    REQUIRE(pv.size() == 3);
    CHECK(pv[0] == std::pair<uint32_t, std::string>{1, "Warning; Illegal PV move - move e5e4 from B"});
    CHECK(pv[2].first == 2);
    CHECK(pv[2].second == "Warning; Too long mating PV - from A");
}

TEST_CASE("state: a tournament written before a PGN setting existed still opens") {
    const auto dir = freshDir("state-old-spec");
    TournamentState::create(dir, matchSpec(2));
    auto spec = nlohmann::json::parse(readAll(dir / "tournament.json"));
    REQUIRE(spec["conditions"]["pgn"].erase("track_wdl") == 1);
    std::ofstream(dir / "tournament.json", std::ios::binary | std::ios::trunc) << spec.dump(2);
    const auto state = TournamentState::open(dir);
    CHECK_FALSE(state->spec().conditions.pgn.track_wdl);
}

TEST_CASE("state: one opening per round, the next of the book for each round, kept on reopening") {
    const auto dir  = freshDir("state-per-round");
    const auto book = dir.parent_path() / "fcg-tour-per-round.epd";
    {
        std::ofstream out(book);
        for (const auto* fen : {"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1",
                                "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq - 0 1",
                                "rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR b KQkq - 0 1",
                                "rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b KQkq - 1 1"}) {
            out << fen << "\n";
        }
    }
    TournamentSpec spec;
    spec.name                      = "per round";
    spec.format                    = Format::RoundRobin;
    spec.opening_per_round         = true;
    spec.conditions.opening.file   = book.string();
    spec.conditions.opening.format = fastchess::FormatType::EPD;
    spec.conditions.opening.order  = fastchess::OrderType::SEQUENTIAL;
    spec.conditions.opening.start  = 1;
    for (int i = 0; i < 4; ++i) {
        Participant p;
        p.id          = i;
        p.engine.name = std::string(1, char('A' + i));
        spec.participants.push_back(p);
    }

    // 4 engines: 3 rounds of 2 encounters. Half of them played before a reopening.
    std::map<uint32_t, std::set<std::string>> by_round;
    const auto play = [&](TournamentState& state, int games) {
        for (int i = 0; i < games; ++i) {
            auto task = state.claimNext();
            REQUIRE(task);
            by_round[task->encounter.round].insert(task->opening.fen_epd);
            state.complete(*task, gameData(*task, "1/2-1/2"));
        }
    };
    {
        auto state = TournamentState::create(dir, spec);
        play(*state, 6);
    }
    auto state = TournamentState::open(dir);
    play(*state, 6);
    CHECK(state->finished());

    REQUIRE(by_round.size() == 3);
    std::set<std::string> all;
    for (const auto& [round, fens] : by_round) {
        CHECK(fens.size() == 1);  // every encounter of the round, the same
        all.insert(*fens.begin());
    }
    CHECK(all.size() == 3);  // a new one for each round
    CHECK(by_round[1].count("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1") == 1);

    // Without the rule, each encounter takes the next one.
    spec.opening_per_round = false;
    const auto other       = freshDir("state-per-encounter");
    auto plain             = TournamentState::create(other, spec);
    std::set<std::string> first_round;
    for (int i = 0; i < 4; ++i) {
        auto task = plain->claimNext();
        REQUIRE(task);
        if (task->encounter.round == 1) first_round.insert(task->opening.fen_epd);
        plain->complete(*task, gameData(*task, "1/2-1/2"));
    }
    CHECK(first_round.size() == 2);
}

namespace {

// The (Round, White) of each game of a PGN file, in file order.
std::vector<std::pair<std::string, std::string>> gamesOf(const fs::path& file) {
    const auto text = readAll(file);
    std::vector<std::pair<std::string, std::string>> games;
    const auto tag = [&](std::size_t from, const std::string& name) {
        const auto at    = text.find("[" + name + " \"", from);
        const auto start = at + name.size() + 3;
        return text.substr(start, text.find('"', start) - start);
    };
    for (auto pos = text.find("[Event "); pos != std::string::npos; pos = text.find("[Event ", pos + 1)) {
        games.emplace_back(tag(pos, "Round"), tag(pos, "White"));
    }
    return games;
}

const std::vector<std::pair<std::string, std::string>> IN_ORDER = {{"1", "A"}, {"1", "B"}, {"2", "A"},
                                                                   {"2", "B"}, {"3", "A"}, {"3", "B"}};

}  // namespace

TEST_CASE("state: the PGN file of the user's gets the games in schedule order, appended, across a resume") {
    const auto dir = freshDir("pgn-out");
    const auto out = dir.parent_path() / "fcg-tour-pgn-out-all.pgn";
    {
        std::ofstream old(out, std::ios::binary | std::ios::trunc);
        old << "[Event \"older\"]\n[Round \"9\"]\n[White \"X\"]\n\n1. e4 *\n\n";
    }
    auto spec    = matchSpec(6);
    spec.pgn_out = out.string();

    {
        auto state = TournamentState::create(dir, spec);
        std::vector<GameTask> t;
        for (int i = 0; i < 4; ++i) t.push_back(*state->claimNext());

        // Games 1 and 3 end first: they wait for game 0.
        state->complete(t[1], gameData(t[1], "1-0"));
        state->complete(t[3], gameData(t[3], "1-0"));
        CHECK(gamesOf(out).size() == 1);

        // Game 0 lets game 1 out; game 3 still waits for game 2, which the stop interrupts.
        state->complete(t[0], gameData(t[0], "1-0"));
        CHECK(gamesOf(out).size() == 3);
        CHECK(state->pgnOutError().empty());
    }

    auto state = TournamentState::open(dir);
    CHECK(gamesOf(out).size() == 3);  // nothing written twice, game 3 held still
    while (auto task = state->claimNext()) state->complete(*task, gameData(*task, "1/2-1/2"));
    CHECK(state->finished());

    auto games = gamesOf(out);
    REQUIRE(games.size() == 7);
    CHECK(games.front().first == "9");  // what was there stays
    games.erase(games.begin());
    CHECK(games == IN_ORDER);
    CHECK(gamesOf(dir / "games.pgn").size() == 6);
}

TEST_CASE("state: the PGN file of the user's gets what is left when the tournament ends, and waits out failures") {
    const auto dir = freshDir("pgn-out-sprt");
    const auto out = dir / "missing-folder";  // a folder for now: it cannot be written
    fs::create_directories(out);

    auto spec                    = matchSpec();
    spec.pgn_out                 = out.string();
    spec.pgn_out_chessbase       = true;
    spec.conditions.sprt.enabled = true;
    spec.conditions.sprt.elo0    = 0;
    spec.conditions.sprt.elo1    = 5;
    spec.conditions.sprt.alpha   = 0.05;
    spec.conditions.sprt.beta    = 0.05;
    spec.conditions.sprt.model   = "normalized";

    auto state  = TournamentState::create(dir, spec);
    auto first  = state->claimNext();
    auto second = state->claimNext();  // runs until the end
    REQUIRE(second);
    state->complete(*first, gameData(*first, "1/2-1/2"));
    CHECK_FALSE(state->pgnOutError().empty());

    fs::remove_all(out);
    int played = 1;
    while (auto task = state->claimNext()) {
        const bool a_white = task->white.name == "A";
        state->complete(*task, gameData(*task, task->game == 0 ? (a_white ? "1-0" : "0-1") : "1/2-1/2"));
        REQUIRE(++played < 5000);
    }
    CHECK(state->finishReason() == "sprt_h1");
    CHECK(state->pgnOutError().empty());

    // Every game played but the second, which never ended, in the ChessBase style.
    CHECK(gamesOf(out).size() == static_cast<std::size_t>(played));
    CHECK(readAll(out) == readAll(dir / "games-chessbase.pgn"));
}

TEST_CASE("state: a replayed game goes to the PGN file of the user's when it ends, without holding the others") {
    const auto dir = freshDir("pgn-out-replay");
    auto spec      = matchSpec(6);
    spec.pgn_out   = (dir / "all.pgn").string();
    auto state     = TournamentState::create(dir, spec);

    for (int i = 0; i < 2; ++i) {
        auto task = state->claimNext();
        state->complete(*task, gameData(*task, "1-0"));
    }
    Edit edit;
    edit.kind  = Edit::Kind::VoidGames;
    edit.games = {{0, 0}};
    state->applyEdit(edit);

    auto replay = state->claimNext();
    REQUIRE(replay);
    CHECK(replay->number() == 0);
    auto next = state->claimNext();
    REQUIRE(next);
    CHECK(next->number() == 2);
    state->complete(*next, gameData(*next, "1-0"));
    CHECK(gamesOf(dir / "all.pgn").size() == 3);  // game 0 was written once already: it holds nothing
    state->complete(*replay, gameData(*replay, "0-1"));

    const std::vector<std::pair<std::string, std::string>> expected = {{"1", "A"}, {"1", "B"}, {"2", "A"}, {"1", "A"}};
    CHECK(gamesOf(dir / "all.pgn") == expected);
}

TEST_CASE("state: games recorded by many threads at once resume exactly, each game where the journal says") {
    const auto dir = freshDir("concurrent");
    auto spec      = matchSpec(100);
    spec.pgn_out   = (dir / "all.pgn").string();

    std::vector<GameResult> before;
    {
        auto state = TournamentState::create(dir, spec);
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&state, t] {
                // Stopped abruptly past 60 games: the games claimed then are never recorded.
                for (int i = 0; state->results().size() < 60; ++i) {
                    const auto task = state->claimNext();
                    if (!task) break;
                    state->complete(*task, gameData(*task, (i + t) % 3 == 0 ? "1-0" : "1/2-1/2"));
                }
            });
        }
        for (auto& thread : threads) thread.join();
        before = state->results();
        CHECK(state->pgnOutError().empty());
    }
    REQUIRE(before.size() >= 60);

    const auto roundOf = [](const std::string& game) {
        const auto at = game.find("[Round \"");
        return game.substr(at + 8, game.find('"', at + 8) - at - 8);
    };
    auto state       = TournamentState::open(dir);
    const auto after = state->results();
    REQUIRE(after.size() == before.size());
    for (std::size_t i = 0; i < after.size(); ++i) {
        CHECK(after[i].number() == before[i].number());
        CHECK(after[i].pgn.offset == before[i].pgn.offset);
        CHECK(after[i].pgn_chessbase.offset == before[i].pgn_chessbase.offset);
        const auto game    = readSpan(state->pgnPath(), after[i].pgn);
        const auto game_cb = readSpan(state->chessbasePgnPath(), after[i].pgn_chessbase);
        REQUIRE(game);
        REQUIRE(game_cb);
        CHECK(roundOf(*game) == std::to_string(after[i].encounter + 1));
        CHECK(roundOf(*game_cb) == std::to_string(after[i].encounter + 1));
    }

    std::set<uint64_t> numbers;
    for (const auto& r : after) CHECK(numbers.insert(r.number()).second);
    while (auto task = state->claimNext()) {
        CHECK(numbers.insert(task->number()).second);
        state->complete(*task, gameData(*task, "0-1"));
    }
    CHECK(state->finished());
    CHECK(numbers.size() == 100);

    // The PGN file of the user's: every game once, in schedule order.
    const auto out = gamesOf(dir / "all.pgn");
    REQUIRE(out.size() == 100);
    for (std::size_t i = 0; i < out.size(); ++i) CHECK(out[i].first == std::to_string(i / 2 + 1));
}

namespace {

// The journal a crash leaves: only `events` of it reached the disk.
void cutJournal(const fs::path& dir, const std::vector<nlohmann::json>& events) {
    std::ofstream out(dir / "journal.jsonl", std::ios::binary | std::ios::trunc);
    for (const auto& e : events) out << e.dump() << '\n';
}

// The games another PGN viewer sees in a file.
std::size_t countGames(const fs::path& file) {
    const auto text = readAll(file);
    std::size_t n   = 0;
    for (auto pos = text.find("[Event "); pos != std::string::npos; pos = text.find("[Event ", pos + 1)) ++n;
    return n;
}

}  // namespace

TEST_CASE("state: game text whose journal line a crash lost, in the middle of the PGN files, is blanked") {
    const auto dir = freshDir("state-pgn-gap");
    {
        auto state = TournamentState::create(dir, matchSpec(6));
        for (int i = 0; i < 3; ++i) {
            const auto task = state->claimNext();
            state->complete(*task, gameData(*task, "1-0"));
        }
    }
    const auto size    = fs::file_size(dir / "games.pgn");
    const auto size_cb = fs::file_size(dir / "games-chessbase.pgn");

    // Games recorded at once by several threads: the line of a later game reached the disk, not that of game 1.
    auto events = Journal::read(dir / "journal.jsonl");
    PgnSpan lost, lost_cb;
    for (auto it = events.begin(); it != events.end(); ++it) {
        if (it->value("t", "") != "game_finished" || it->value("encounter", 9) != 0 || it->value("game", 9) != 1) {
            continue;
        }
        lost    = {(*it)["pgn"][0].get<uint64_t>(), (*it)["pgn"][1].get<uint32_t>()};
        lost_cb = {(*it)["cb"][0].get<uint64_t>(), (*it)["cb"][1].get<uint32_t>()};
        events.erase(it);
        break;
    }
    REQUIRE(lost.length > 0);
    cutJournal(dir, events);

    auto state = TournamentState::open(dir);
    // The games after it keep their places; its text is now blank lines.
    CHECK(fs::file_size(dir / "games.pgn") == size);
    CHECK(fs::file_size(dir / "games-chessbase.pgn") == size_cb);
    CHECK(countGames(dir / "games.pgn") == 2);
    CHECK(countGames(dir / "games-chessbase.pgn") == 2);
    CHECK(*readSpan(dir / "games.pgn", lost) == std::string(lost.length, '\n'));
    CHECK(*readSpan(dir / "games-chessbase.pgn", lost_cb) == std::string(lost_cb.length, '\n'));
    REQUIRE(state->results().size() == 2);
    for (const auto& r : state->results()) {
        CHECK(readSpan(dir / "games.pgn", r.pgn)->find("[Event ") == 0);
        CHECK(readSpan(dir / "games-chessbase.pgn", r.pgn_chessbase)->find("[Event ") == 0);
    }

    // Game 1 is played again; at the end every game is there once.
    while (auto task = state->claimNext()) state->complete(*task, gameData(*task, "1/2-1/2"));
    CHECK(state->finished());
    CHECK(countGames(dir / "games.pgn") == 6);
    CHECK(countGames(dir / "games-chessbase.pgn") == 6);

    // Nothing more to do on the next opening.
    const auto text = readAll(dir / "games.pgn");
    state.reset();
    state = TournamentState::open(dir);
    CHECK(readAll(dir / "games.pgn") == text);
    CHECK(state->results().size() == 6);
}

TEST_CASE("state: games dropped by an edit while their text was written leave none in the PGN files") {
    const auto dir      = freshDir("state-pgn-void");
    std::size_t results = 0;
    {
        auto state = TournamentState::create(dir, matchSpec(200));
        std::mutex mutex;
        std::set<std::pair<uint32_t, int>> running;
        std::atomic<bool> over{false};

        std::vector<std::thread> threads;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&] {
                while (auto task = state->claimNext()) {
                    const std::pair<uint32_t, int> game{task->encounter.index, task->game};
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        running.insert(game);
                    }
                    state->complete(*task, gameData(*task, "1-0"));
                    std::lock_guard<std::mutex> lock(mutex);
                    running.erase(game);
                }
                over = true;
            });
        }
        // Games voided without replay while they are played, some of them while their text is being written: the
        // journal never points to that text.
        while (!over) {
            Edit edit;
            edit.kind   = Edit::Kind::VoidGames;
            edit.replay = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!running.empty()) edit.games = {*running.begin()};
            }
            try {
                if (!edit.games.empty()) state->applyEdit(edit);
            } catch (const std::exception&) {
                // The game was voided already.
            }
            std::this_thread::yield();
        }
        for (auto& thread : threads) thread.join();
        CHECK(state->finished());
        results = state->results().size();

        CHECK(countGames(dir / "games.pgn") == results);
        CHECK(countGames(dir / "games-chessbase.pgn") == results);
    }

    auto state = TournamentState::open(dir);
    CHECK(state->results().size() == results);
    CHECK(countGames(dir / "games.pgn") == results);
    CHECK(countGames(dir / "games-chessbase.pgn") == results);
}

TEST_CASE("state: a crash in the middle of a write to the PGN file of the user's writes no game twice, loses none") {
    const auto dir          = freshDir("pgn-out-crash");
    const auto out          = dir.parent_path() / "fcg-tour-pgn-out-crash.pgn";
    const std::string older = "[Event \"older\"]\n[Round \"9\"]\n[White \"X\"]\n\n1. e4 *\n\n";
    {
        std::ofstream file(out, std::ios::binary | std::ios::trunc);
        file << older;
    }
    auto spec    = matchSpec(6);
    spec.pgn_out = out.string();
    {
        auto state = TournamentState::create(dir, spec);
        for (int i = 0; i < 2; ++i) {
            const auto task = state->claimNext();
            state->complete(*task, gameData(*task, "1-0"));
        }
    }
    const auto full = readAll(out);
    REQUIRE(gamesOf(out).size() == 3);

    // The write of game 1 was journaled before, not after: the crash came in between.
    auto events = Journal::read(dir / "journal.jsonl");
    REQUIRE(!events.empty());
    REQUIRE(events.back().value("t", "") == "pgn_out");
    REQUIRE_FALSE(events.back().contains("begun"));
    events.pop_back();
    REQUIRE(events.back().contains("begun"));
    const auto at  = events.back()["at"].get<uint64_t>();
    const auto len = events.back()["len"].get<uint64_t>();
    CHECK(at + len == full.size());
    CHECK(events.back()["pgn"].empty());  // what older versions read: nothing written
    cutJournal(dir, events);

    std::string expected = full;
    SUBCASE("the games reached the file: not written again") {}
    SUBCASE("the crash came before the write: the games are written") { fs::resize_file(out, at); }
    SUBCASE("the write was cut short: it is completed") { fs::resize_file(out, at + len / 2); }
    SUBCASE("another program wrote there first: the games are found further on, nothing is cut") {
        const std::string other = "[Event \"other\"]\n[Round \"7\"]\n[White \"Y\"]\n\n1. d4 *\n\n";
        expected                = full.substr(0, at) + other + full.substr(at);
        std::ofstream file(out, std::ios::binary | std::ios::trunc);
        file << expected;
    }
    SUBCASE("the write was damaged: the games are written again after it, nothing is cut") {
        auto damaged = full;
        damaged.replace(at + 3, 5, "xxxxx");
        expected = damaged + full.substr(at);
        std::ofstream file(out, std::ios::binary | std::ios::trunc);
        file << damaged;
    }

    auto state = TournamentState::open(dir);
    CHECK(readAll(out) == expected);
    CHECK(state->pgnOutError().empty());

    // Settled in the journal: the next opening leaves the file alone.
    state.reset();
    state = TournamentState::open(dir);
    CHECK(readAll(out) == expected);

    // The rest of the games follow, each once.
    while (auto task = state->claimNext()) state->complete(*task, gameData(*task, "1/2-1/2"));
    CHECK(state->finished());
    const auto text = readAll(out);
    REQUIRE(text.size() > expected.size());
    CHECK(text.compare(0, expected.size(), expected) == 0);
    auto games = gamesOf(out);
    REQUIRE(games.size() >= 4);
    games.erase(games.begin(), games.end() - 4);
    const std::vector<std::pair<std::string, std::string>> rest(IN_ORDER.begin() + 2, IN_ORDER.end());
    CHECK(games == rest);
}

TEST_CASE("engine options: as a Windows engine reads them, ASCII and the spec untouched") {
    CHECK(forEngine(std::string("C:\\Engines\\book.bin")) == "C:\\Engines\\book.bin");
    CHECK(forEngine(std::string("")).empty());
#ifdef _WIN32
    // Windows-1252 on the test machines: É is one byte there. A name with Cyrillic has no place in it and no short
    // name when the file does not exist: sent as it is.
    if (forEngine(std::string("\xC3\x89")) == "\xC9") {
        MESSAGE("Windows-1252");
        CHECK(forEngine(std::string("C:\\\xC3\x89"
                                    "checs\\book.bin")) ==
              "C:\\\xC9"
              "checs\\book.bin");
    }
    CHECK(forEngine(std::string("C:\\\xD0\x96\\none.bin")) == "C:\\\xD0\x96\\none.bin");
#else
    CHECK(forEngine(std::string("/home/h\xC3\xA9l\xC3\xA8ne/book.bin")) == "/home/h\xC3\xA9l\xC3\xA8ne/book.bin");
#endif

    auto spec                           = matchSpec();
    spec.participants[0].engine.options = {{"BookFile",
                                            "C:\\\xC3\x89"
                                            "checs\\b.bin"},
                                           {"Hash", "16"}};
    auto state                          = TournamentState::create(freshDir("engine-text"), spec);
    const auto task                     = state->claimNext();
    REQUIRE(task);
    const auto& mine = task->game == 0 ? task->white : task->black;
    CHECK(mine.options[1].second == "16");
    CHECK(mine.options[0].second == forEngine(std::string("C:\\\xC3\x89"
                                                          "checs\\b.bin")));
    CHECK(state->spec().participants[0].engine.options[0].second ==
          "C:\\\xC3\x89"
          "checs\\b.bin");  // kept in UTF-8
}
