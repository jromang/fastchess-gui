#include <doctest/doctest.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include <tour/engine_text.hpp>
#include <tour/journal.hpp>
#include <tour/state.hpp>

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
