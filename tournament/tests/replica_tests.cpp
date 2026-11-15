// A read-only copy of a tournament another program plays, kept current from the journal lines it writes: what the
// window holds of a tournament played in a process of its own.

#include <doctest/doctest.hpp>

#include <filesystem>
#include <mutex>
#include <vector>

#include <tour/edits.hpp>
#include <tour/journal.hpp>
#include <tour/start_failures.hpp>
#include <tour/state.hpp>

using namespace fcg::tour;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const std::string& name) {
    const auto dir = fs::temp_directory_path() / ("fcg-tour-replica-" + name);
    fs::remove_all(dir);
    return dir;
}

TournamentSpec roundRobin(int players, int cycles) {
    TournamentSpec spec;
    spec.format = Format::RoundRobin;
    spec.cycles = cycles;
    for (int i = 0; i < players; ++i) {
        Participant p;
        p.id          = i;
        p.engine.name = std::string(1, char('A' + i));
        spec.participants.push_back(p);
    }
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

int play(TournamentState& state, int n) {
    int played = 0;
    while (n < 0 || played < n) {
        const auto task = state.claimNext();
        if (!task) break;
        state.complete(*task, data(*task, played % 3 == 0 ? "1-0" : played % 3 == 1 ? "1/2-1/2" : "0-1"));
        ++played;
    }
    return played;
}

// The journal lines the writer wrote, as its listener told them.
struct Lines {
    struct Line {
        uint64_t at = 0, end = 0;
        nlohmann::json event;
    };
    std::mutex mutex;
    std::vector<Line> lines;

    Journal::Listener listener() {
        return [this](uint64_t at, uint64_t end, const nlohmann::json& e) {
            std::lock_guard<std::mutex> lock(mutex);
            lines.push_back({at, end, e});
        };
    }

    // Follows, in `copy`, the lines it does not hold yet; false when it must be opened again.
    bool follow(TournamentState& copy) {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& l : lines) {
            if (l.at < copy.journalSize()) continue;
            REQUIRE(l.at == copy.journalSize());
            if (!copy.follow(l.event, l.end)) return false;
        }
        return true;
    }
};

void checkSame(const TournamentState& writer, const TournamentState& copy) {
    const auto a = writer.results(), b = copy.results();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].number() == b[i].number());
        CHECK(a[i].result == b[i].result);
        CHECK(a[i].voided == b[i].voided);
        CHECK(a[i].pgn.offset == b[i].pgn.offset);
    }
    CHECK(writer.finished() == copy.finished());
    CHECK(writer.totalGames() == copy.totalGames());
    CHECK(writer.spec().participants.size() == copy.spec().participants.size());
    for (int x = 0; x < 4; ++x) {
        for (int y = x + 1; y < 4; ++y) CHECK(writer.pairStats(x, y).points() == copy.pairStats(x, y).points());
    }
    const auto ra = writer.scheduleRows(), rb = copy.scheduleRows();
    REQUIRE(ra.size() == rb.size());
    for (std::size_t i = 0; i < ra.size(); ++i) {
        for (int g = 0; g < 2; ++g) {
            // The copy knows the running games from the status the process sends, not from its journal.
            if (ra[i].status[g] == GameStatus::Running) continue;
            CHECK(ra[i].status[g] == rb[i].status[g]);
        }
    }
}

}  // namespace

TEST_CASE("replica: a read-only copy follows the journal lines of the program playing the tournament") {
    const auto dir = freshDir("follow");
    auto writer    = TournamentState::create(dir, roundRobin(4, 1));
    Lines lines;
    writer->setJournalListener(lines.listener());

    // A few games before the copy is opened: it reads them from the files.
    play(*writer, 3);
    auto copy = TournamentState::open(dir, true);
    CHECK(copy->readOnly());
    CHECK(copy->journalSize() == fs::file_size(dir / "journal.jsonl"));
    checkSame(*writer, *copy);

    // Then games and an edit, followed line by line.
    const auto version = copy->version();
    play(*writer, 4);
    Edit cycles;
    cycles.kind   = Edit::Kind::SetCycles;
    cycles.cycles = 2;
    writer->applyEdit(cycles);
    play(*writer, 2);
    REQUIRE(lines.follow(*copy));
    CHECK(copy->version() != version);
    CHECK(copy->journalSize() == fs::file_size(dir / "journal.jsonl"));
    checkSame(*writer, *copy);
    CHECK(copy->totalGames() == 24);

    // The status the process sends: the running games, whether the last change can be undone.
    TournamentState::ReplicaStatus status;
    status.running  = {copy->results().size()};
    status.can_undo = true;
    copy->setReplicaStatus(status);
    CHECK(copy->canUndo());
    CHECK_FALSE(copy->undoLasts());

    // Played to the end.
    play(*writer, -1);
    REQUIRE(lines.follow(*copy));
    checkSame(*writer, *copy);
    CHECK(copy->finished());

    // A copy writes nothing and follows nothing of its own.
    CHECK_THROWS(copy->applyEdit(cycles));
    CHECK_THROWS(writer->follow(nlohmann::json{{"t", "finished"}}, 0));
}

TEST_CASE("replica: an undone edit is not followed: the copy is opened again, and holds what the writer holds") {
    const auto dir = freshDir("undo");
    auto writer    = TournamentState::create(dir, roundRobin(4, 1));
    Lines lines;
    writer->setJournalListener(lines.listener());
    auto copy = TournamentState::open(dir, true);

    play(*writer, 2);
    Edit withdraw;
    withdraw.kind        = Edit::Kind::Withdraw;
    withdraw.participant = 3;
    writer->applyEdit(withdraw);
    REQUIRE(lines.follow(*copy));
    CHECK_FALSE(copy->spec().participant(3)->active);

    writer->undoEdit();
    CHECK_FALSE(lines.follow(*copy));
    copy = TournamentState::open(dir, true);
    CHECK(copy->spec().participant(3)->active);
    checkSame(*writer, *copy);
}

TEST_CASE("replica: games, edits and start failures travel as JSON and come back the same") {
    GameResult r;
    r.encounter     = 7;
    r.game          = 1;
    r.white         = 3;
    r.black         = 2;
    r.result        = "0-1";
    r.termination   = fastchess::MatchTermination::TIMEOUT;
    r.reason        = "White loses on time";
    r.plies         = 81;
    r.duration_ms   = 12345;
    r.pgn           = {1000, 400};
    r.pgn_chessbase = {2000, 500};
    r.finished_at   = 1700000000000;
    r.pv_warnings   = {{12, "illegal PV move"}};
    r.set           = ResultSet{"a test", "1-0", fastchess::MatchTermination::NORMAL, "White mates"};
    const auto back = gameResultFromJson(gameResultJson(r));
    CHECK(back.number() == r.number());
    CHECK(back.result == r.result);
    CHECK(back.termination == r.termination);
    CHECK(back.reason == r.reason);
    CHECK(back.pgn.offset == 1000);
    CHECK(back.pgn_chessbase.length == 500);
    CHECK(back.pv_warnings == r.pv_warnings);
    REQUIRE(back.set);
    CHECK(back.set->played_reason == "White mates");

    Edit e;
    e.kind        = Edit::Kind::VoidGames;
    e.engine.name = "New";
    e.engine.cmd  = "/bin/new";
    e.games       = {{3, 0}, {4, 1}};
    e.replay      = false;
    e.why         = "crashes";
    const Edit f  = nlohmann::json(e).get<Edit>();
    CHECK(f.kind == Edit::Kind::VoidGames);
    CHECK(f.engine.name == "New");
    CHECK(f.engine.cmd == "/bin/new");
    CHECK(f.games == e.games);
    CHECK_FALSE(f.replay);
    CHECK(f.why == "crashes");
    CHECK_THROWS(nlohmann::json{{"kind", 99}}.get<Edit>());

    StartFailures failures;
    StartFailures::Engine engine;
    engine.participant = 2;
    engine.name        = "Broken";
    engine.error       = "cannot execute";
    engine.failed      = true;
    const auto before  = failures.version();
    failures.assign({engine}, true);
    CHECK(failures.version() != before);
    CHECK(failures.stalled());
    REQUIRE(failures.engine(2));
    CHECK(failures.engine(2)->error == "cannot execute");
}
