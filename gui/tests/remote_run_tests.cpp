// Tournaments played in a process of their own (fastchess-gui --run <folder> --control stdio), driven as the window
// drives them, with fastchess' random mover engine.

#include <doctest/doctest.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "../../tournament/tests/self_path.hpp"
#include "child_process.hpp"
#include "console.hpp"
#include "local_socket.hpp"
#include "remote_run.hpp"
#include "run.hpp"
#include "storage.hpp"

#ifdef _WIN32
#    include <windows.h>
#endif

using namespace fcg;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

std::string randomMover() { return (fcg::test::selfPath().parent_path() / FCG_RANDOM_MOVER).string(); }
// The program a tournament plays in: where the build put it, else beside the tests (copied to another machine).
std::string guiProgram() {
    const fs::path built(FCG_GUI_PROGRAM);
    std::error_code ec;
    if (fs::is_regular_file(built, ec)) return built.string();
    return (fcg::test::selfPath().parent_path() / built.filename()).string();
}

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

// A variable of the mock engines for the time of a test, unset however the test ends: a REQUIRE that fails must not
// leave the next tests' engines slow (on Windows a 2 ms wait lasts a timer tick, about 16 ms).
struct ScopedEnv {
    ScopedEnv(const char* name, const char* value) : name_(name) { setEnv(name, value); }
    ~ScopedEnv() { setEnv(name_, nullptr); }
    ScopedEnv(const ScopedEnv&)            = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

   private:
    const char* name_;
};

std::vector<EngineEntry> engines(int n) {
    std::vector<EngineEntry> out(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        out[static_cast<std::size_t>(i)].name = "random-" + std::string(1, char('a' + i));
        out[static_cast<std::size_t>(i)].cmd  = randomMover();
    }
    return out;
}

MatchSetup roundRobin(int players, int cycles) {
    MatchSetup setup;
    setup.format = TournamentFormat::RoundRobin;
    setup.name   = "remote test";
    for (int i = 0; i < players; ++i) {
        ParticipantSetup p;
        p.name = "random-" + std::string(1, char('a' + i));
        setup.participants.push_back(p);
    }
    setup.cycles           = cycles;
    setup.limit            = LimitKind::Nodes;
    setup.nodes            = 1;
    setup.concurrency_auto = false;
    setup.concurrency      = 2;
    setup.notify           = false;
    // Games of random moves can last hundreds of plies, each a timer tick on Windows with FCG_MOCK_MOVE_MS: these
    // tests are about the tournament's process, not the length of its games, which end as draws after 40 moves.
    setup.maxmoves_enabled = true;
    setup.maxmoves         = 40;
    return setup;
}

// A tournament folder made as the window makes it before it starts its process.
fs::path makeTournament(const fs::path& profile, const MatchSetup& setup, int players) {
    fs::remove_all(profile);
    fs::create_directories(profile);
    const Storage storage(profile);
    storage.saveEngines(engines(players));
    const auto dir = storage.runsDir() / makeTournamentDirName(setup.name);
    const auto run = buildRun(setup, engines(players), topologyFromCores({{0, 0}, {0, 1}, {0, 2}, {0, 3}}), dir);
    (void)tour::TournamentState::create(dir, run.spec);  // closed at once: the process opens it
    return dir;
}

struct Harness {
    RemoteRun run;
    std::vector<GameRecord> games;
    std::vector<fastchess::Stats> stats;
    int most_running = 0;  // the most games seen being played at once since `watch_from`
    bool watching    = false;
    bool saw_moves   = false;

    void pump() {
        run.update();
        if (auto* obs = run.observer()) {
            obs->drain(games, stats);
            for (int i = 0; i < obs->slotCount(); ++i) {
                SlotState slot;
                if (obs->copySlot(i, slot) && slot.active && !slot.moves.empty()) saw_moves = true;
            }
        }
        if (watching) most_running = std::max(most_running, run.runningGames());
    }

    template <typename F>
    bool waitFor(F done, std::chrono::milliseconds timeout = 60s) {  // Windows starts processes slowly
        const auto end = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < end) {
            pump();
            if (done()) return true;
            std::this_thread::sleep_for(5ms);
        }
        return false;
    }
};

}  // namespace

TEST_CASE("remote: a tournament in a process of its own; its copy follows; pause, edits, allowance, stop when idle") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }
    const ScopedEnv mock_move_ms("FCG_MOCK_MOVE_MS", "2");
    const auto profile = fs::temp_directory_path() / "fcg-remote-tests";
    const auto setup   = roundRobin(3, 4);
    const auto dir     = makeTournament(profile, setup, 3);
    const auto marked  = Console::instance().added();

    Harness h;
    RemoteStart start;
    start.program = guiProgram();
    start.profile = profile;
    start.dir     = dir;
    REQUIRE(h.run.start(start).empty());
    CHECK(h.run.remote());
    CHECK(h.run.running());
    REQUIRE(h.run.tournament());
    CHECK(h.run.tournament()->readOnly());
    CHECK(h.run.info().total_games == 24);
    REQUIRE(h.run.observer());
    CHECK(h.run.observer()->slotCount() == 2);

    // Games come, with their moves while they are played.
    REQUIRE(h.waitFor([&] { return h.games.size() >= 3; }));
    CHECK(h.saw_moves);
    CHECK(h.run.tournament()->results().size() >= 3);
    CHECK(h.run.pid() > 0);

    // Paused: the running games end, and none starts.
    h.run.setPaused(true);
    CHECK(h.run.paused());
    REQUIRE(h.waitFor(
        [&] { return h.run.status().paused && h.run.runningGames() == 0 && h.run.status().open_pairs == 0; }));
    const auto at_pause = h.run.tournament()->results().size();
    std::this_thread::sleep_for(600ms);
    h.pump();
    CHECK(h.run.tournament()->results().size() == at_pause);

    // A change while paused: the copy has it when applyEdit() returns; it can be undone until a game starts.
    tour::Edit cycles;
    cycles.kind   = tour::Edit::Kind::SetCycles;
    cycles.cycles = 5;
    CHECK(h.run.applyEdit(cycles).empty());
    CHECK(h.run.tournament()->totalGames() == 30);
    CHECK(h.run.info().total_games == 30);
    REQUIRE(h.waitFor([&] { return h.run.tournament()->canUndo(); }, 5s));
    CHECK(h.run.undoEdit().empty());
    CHECK(h.run.takeReopened());
    CHECK(h.run.tournament()->totalGames() == 24);
    // One that cannot be made is refused at once.
    tour::Edit bad = cycles;
    bad.cycles     = 0;
    CHECK_FALSE(h.run.applyEdit(bad).empty());
    CHECK(h.run.applyEdit(cycles).empty());

    // One game at a time: a game of single-thread engines takes 1 thread.
    tour::Allowance one;
    one.threads         = 1;
    one.one_game_always = false;
    h.run.setAllowance(one);
    h.run.setPaused(false);
    REQUIRE(h.waitFor([&] { return h.run.status().allowance == one && !h.run.status().paused; }, 5s));
    REQUIRE(h.waitFor([&] { return h.run.tournament()->results().size() >= at_pause + 3; }));
    // The two games let through before the allowance (they waited at the pause gate) may still be in the last state
    // the process sent: it sends one every 200 ms, as it stood then, and fast games end three results earlier. Watched
    // from a state with one game at most, which every later one is.
    REQUIRE(h.waitFor([&] { return h.run.runningGames() <= 1; }, 5s));
    h.watching = true;
    REQUIRE(h.waitFor([&] { return h.run.tournament()->results().size() >= at_pause + 6; }));
    CHECK(h.most_running <= 1);
    CHECK(h.run.status().in_flight.threads <= 1);

    // Stopped once the running games are over: nothing is lost.
    h.run.stopWhenIdle();
    CHECK(h.run.stoppingWhenIdle());
    REQUIRE(h.waitFor([&] { return h.run.state() == Run::State::Finished; }));
    CHECK(h.run.takeFinishedEvent());
    CHECK(h.run.stoppedByUser());
    CHECK(h.run.error().empty());
    CHECK(h.run.end().exit_code == 3);
    CHECK_FALSE(h.run.end().finished);

    // The copy holds what the files hold, and the window's lists every game.
    const auto fresh = tour::TournamentState::open(dir, true);
    REQUIRE(fresh->results().size() == h.run.tournament()->results().size());
    CHECK(fresh->totalGames() == 30);
    CHECK(h.games.size() == fresh->results().size());
    for (std::size_t i = 0; i < fresh->results().size(); ++i)
        CHECK(fresh->results()[i].pgn.offset == h.run.tournament()->results()[i].pgn.offset);
    // The process measured its engines (processor.tsv), and the window counted it.
    std::ifstream tsv(dir / "processor.tsv", std::ios::binary);
    std::size_t lines = 0;
    for (std::string line; std::getline(tsv, line);) lines += parseGameCpuLine(line) ? 1 : 0;
    CHECK(lines == fresh->results().size());
    const auto totals = h.run.observer()->totals();
    REQUIRE(totals.size() == 3);
    CHECK(totals[0].moves > 0);
    // Its log reached the window's, under the tournament's name.
    bool logged = false;
    std::vector<Console::Line> console;
    uint64_t version = 0;
    Console::instance().copy(console, version);
    for (const auto& line : console) logged = logged || line.source == "remote test";
    CHECK(logged);
    CHECK(Console::instance().added() > marked);
    setEnv("FCG_MOCK_MOVE_MS", nullptr);

    // Once its process ended, a change is made here, and can be undone.
    tour::Edit more = cycles;
    more.cycles     = 6;
    CHECK(h.run.applyEdit(more).empty());
    CHECK(h.run.tournament()->totalGames() == 36);
    CHECK_FALSE(h.run.tournament()->readOnly());
    CHECK(h.run.undoEdit().empty());
    CHECK(h.run.tournament()->totalGames() == 30);
}

TEST_CASE("remote: a game ended by the operator, then the window going away stops the tournament, resumable") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }
    const ScopedEnv mock_move_ms("FCG_MOCK_MOVE_MS", "30");
    const auto profile = fs::temp_directory_path() / "fcg-remote-tests-end";
    auto setup         = roundRobin(2, 2);
    setup.format       = TournamentFormat::Match;
    setup.engine_a     = "random-a";
    setup.engine_b     = "random-b";
    setup.mode         = MatchMode::Fixed;
    setup.fixed_games  = 20;
    setup.recover      = true;
    fs::remove_all(profile);
    fs::create_directories(profile);
    const Storage storage(profile);
    storage.saveEngines(engines(2));
    const auto dir = storage.runsDir() / makeRunDirName("random-a", "random-b");
    (void)tour::TournamentState::create(dir,
                                        buildRun(setup, engines(2), topologyFromCores({{0, 0}, {0, 1}}), dir).spec);

    int64_t pid = 0;
    {
        Harness h;
        RemoteStart start;
        start.program = guiProgram();
        start.profile = profile;
        start.dir     = dir;
        REQUIRE(h.run.start(start).empty());
        pid = h.run.pid();

        // A game with a few moves played: ended as a draw by the operator.
        SlotState slot;
        int playing = -1;
        REQUIRE(h.waitFor([&] {
            for (int i = 0; i < h.run.observer()->slotCount(); ++i) {
                if (h.run.observer()->copySlot(i, slot) && slot.active && slot.moves.size() >= 4) {
                    playing = i;
                    return true;
                }
            }
            return false;
        }));
        const auto number = slot.game_id - 1;
#ifdef __APPLE__
        // Not offered on macOS yet (see RunController::endGame()).
        CHECK_FALSE(h.run.endGame(playing, number, "1/2-1/2", "a test").empty());
        return;
#endif
        CHECK(h.run.endGame(playing, number, "1/2-1/2", "a test").empty());
        REQUIRE(h.waitFor([&] {
            const auto r = h.run.tournament()->resultOf(number);
            return r && r->set;
        }));
        CHECK(h.run.tournament()->resultOf(number)->result == "1/2-1/2");
        CHECK(h.run.tournament()->resultOf(number)->set->why == "a test");
        // The window goes away: its process stops.
    }
    CHECK(pid > 0);
    setEnv("FCG_MOCK_MOVE_MS", nullptr);

    // The folder is free again, and the tournament can be resumed.
    const auto reopened = tour::TournamentState::open(dir);
    CHECK_FALSE(reopened->finished());
    CHECK(reopened->results().size() < 20);
}

TEST_CASE("remote: a tournament on its socket plays on without the window, which connects to it again") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available on this platform, skipped");
        return;
    }
    const ScopedEnv mock_move_ms("FCG_MOCK_MOVE_MS", "15");
    const auto profile = fs::temp_directory_path() / "fcg-remote-tests-detach";
    // Long enough to play on until the window connects again, however slowly the processes start on a busy machine
    // (the games take their time, the processes the processor's): it is stopped once idle, not played to its end.
    const auto dir = makeTournament(profile, roundRobin(3, 30), 3);

    std::size_t seen = 0;
    Clock::time_point played_by{};  // the process played already then
    {
        Harness h;
        RemoteStart start;
        start.program  = guiProgram();
        start.profile  = profile;
        start.dir      = dir;
        start.detached = true;
        REQUIRE(h.run.start(start).empty());
        REQUIRE(h.waitFor([&] { return h.games.size() >= 2; }, 60s));
        played_by = Clock::now();
        CHECK_FALSE(h.run.connecting());
        CHECK(h.run.pid() > 0);
        seen = h.games.size();
        // The window goes away: its process plays on.
        h.run.disconnect();
    }
    const auto control = local::readControlFile(dir);
    REQUIRE(control);
    const auto size  = fs::file_size(dir / "journal.jsonl");
    const auto until = std::chrono::steady_clock::now() + 30s;
    while (fs::file_size(dir / "journal.jsonl") == size && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(50ms);
    CHECK(fs::file_size(dir / "journal.jsonl") > size);

    // A script talks with it through --attach: the process's hello, then the journal lines after the one it gives.
    {
        ChildProcess attach;
        std::string error;
        REQUIRE(attach.start(guiProgram(), {"--attach", dir.string()}, error));
        REQUIRE(attach.write("{\"hello\":{\"protocol\":1,\"version\":\"test\",\"journal\":0}}\n"));
        std::string got;
        char buffer[4096];
        while (got.find("\"journal\"") == std::string::npos || got.find("\"hello\"") == std::string::npos) {
            const long n = attach.read(buffer, sizeof buffer);
            if (n <= 0) break;
            got.append(buffer, static_cast<std::size_t>(n));
        }
        CHECK(got.find("\"hello\"") != std::string::npos);
        CHECK(got.find("\"journal\"") != std::string::npos);
        attach.kill();
        (void)attach.wait();
    }

    // The window connects again: its copy has every game, and the games in progress as they stand.
    Harness h;
    std::string error;
    std::unique_ptr<Link> link;
    // The script's connection may still be closing: tried again a moment.
    for (int i = 0; i < 50 && !link; ++i) {
        link = local::connect(control->endpoint, error);
        if (!link) std::this_thread::sleep_for(100ms);
    }
    REQUIRE(link);
    REQUIRE(h.run.attach(dir, std::move(link)).empty());
    REQUIRE(h.waitFor([&] { return h.games.size() > seen + 2 && h.saw_moves; }, 60s));
    CHECK(h.run.pid() == control->pid);
    // Its time counts from the process's start, told in its hello, not from this connection.
    CHECK(h.run.info().started < played_by);

    // A second window is turned away: one window at a time talks with a tournament.
    {
        Harness other;
        auto second = local::connect(control->endpoint, error);
        REQUIRE(second);
        REQUIRE(other.run.attach(dir, std::move(second)).empty());
        CHECK(other.waitFor([&] { return other.run.elsewhere(); }, 60s));
    }

    h.run.stopWhenIdle();
    REQUIRE(h.waitFor([&] { return h.run.state() == Run::State::Finished; }, 60s));
    CHECK(h.run.error().empty());
    CHECK(h.run.end().exit_code == 3);
    CHECK_FALSE(local::readControlFile(dir));
    const auto fresh = tour::TournamentState::open(dir, true);
    CHECK(fresh->results().size() == h.run.tournament()->results().size());
    CHECK(h.games.size() == fresh->results().size());
    setEnv("FCG_MOCK_MOVE_MS", nullptr);

    // Nothing plays it now: --attach says so.
    ChildProcess attach;
    REQUIRE(attach.start(guiProgram(), {"--attach", dir.string()}, error));
    attach.closeInput();
    CHECK(attach.wait() == 2);
}
