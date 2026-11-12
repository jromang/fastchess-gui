// Real games with fastchess' random mover engine, through the Runner.

#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <thread>

#include <core/config/config.hpp>
#include <core/globals/globals.hpp>
#include <core/memory/cache.hpp>
#include <core/observer.hpp>
#include <engine/uci_engine.hpp>

#include <tour/runner.hpp>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif

using namespace fcg::tour;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

std::string randomMover() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    const auto n = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    const fs::path self(std::string(buffer, n));
#else
    const fs::path self = fs::read_symlink("/proc/self/exe");
#endif
    return (self.parent_path() / FCG_RANDOM_MOVER).string();
}

TournamentSpec spec(Format format, int players, int cycles, uint64_t max_games = 0) {
    TournamentSpec s;
    s.name                   = "runner test";
    s.format                 = format;
    s.cycles                 = cycles;
    s.max_games              = max_games;
    s.conditions.concurrency = 3;
    s.conditions.recover     = true;
    for (int i = 0; i < players; ++i) {
        Participant p;
        p.id                 = i;
        p.engine.name        = "random" + std::to_string(i);
        p.engine.cmd         = randomMover();
        p.engine.limit.nodes = 1;
        s.participants.push_back(p);
    }
    return s;
}

// Plays `state` on this thread, as the GUI's run thread does. With `stop_after`, stops abruptly once that many
// games are recorded, without waiting for the running ones.
std::string play(TournamentState& state, std::size_t stop_after = 0) {
    auto config         = state.spec().conditions;
    config.games        = 2;
    config.opening.file = "";
    config.pgn.file     = "";
    config.config_name  = "";
    config.output       = fastchess::OutputType::NONE;

    std::vector<fastchess::EngineConfiguration> engines;
    for (const auto& p : state.spec().participants) engines.push_back(p.engine);

    fastchess::atomic::stop                 = false;
    fastchess::atomic::abnormal_termination = false;
    fastchess::config::TournamentConfig     = std::make_unique<fastchess::config::Tournament>(config);
    fastchess::config::EngineConfigs        = std::make_unique<std::vector<fastchess::EngineConfiguration>>(engines);
    fastchess::observer::PauseGate::instance().reset();

    std::atomic_bool done{false};
    std::thread killer;
    if (stop_after > 0) {
        killer = std::thread([&] {
            while (!done && state.results().size() < stop_after) std::this_thread::sleep_for(1ms);
            fastchess::atomic::stop = true;
        });
    }

    std::string error;
    {
        Runner runner(state, nullptr, nullptr);
        runner.start();
        error = runner.error();
    }
    done = true;
    if (killer.joinable()) killer.join();
    fastchess::stopProcesses();
    return error;
}

// Sets an environment variable of the engines started from now on; nullptr removes it.
void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    SetEnvironmentVariableA(name, value);
#else
    if (value) {
        setenv(name, value, 1);
    } else {
        unsetenv(name);
    }
#endif
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::size_t countOf(const std::string& text, const std::string& what) {
    std::size_t n = 0;
    for (auto pos = text.find(what); pos != std::string::npos; pos = text.find(what, pos + 1)) ++n;
    return n;
}

std::size_t countGames(const fs::path& pgn) {
    std::ifstream in(pgn, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t n = 0;
    for (auto pos = text.find("[Event "); pos != std::string::npos; pos = text.find("[Event ", pos + 1)) ++n;
    return n;
}

}  // namespace

TEST_CASE("runner: a round robin plays every encounter, both colours, once") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available, skipped");
        return;
    }

    const auto dir = fs::temp_directory_path() / "fcg-tour-runner-rr";
    fs::remove_all(dir);
    auto s                = spec(Format::RoundRobin, 4, 2);
    s.participants[0].elo = 3500;
    auto state            = TournamentState::create(dir, s);

    CHECK(play(*state).empty());
    CHECK(state->finished());

    const auto results = state->results();
    CHECK(results.size() == 6u * 2u * 2u);

    std::set<uint64_t> numbers;
    std::map<std::pair<int, int>, int> colours;
    for (const auto& r : results) {
        CHECK(numbers.insert(r.number()).second);
        colours[{r.white, r.black}]++;
    }
    // 2 cycles x 2 games: every ordered pair has White twice.
    CHECK(colours.size() == 12);
    for (const auto& [pair, n] : colours) CHECK(n == 2);

    CHECK(countGames(dir / "games.pgn") == results.size());
    CHECK(countGames(dir / "games-chessbase.pgn") == results.size());

    // The rated engine's Elo in both files, after the Result tag; nothing for the others. 3 opponents x 4 games.
    for (const auto* name : {"games.pgn", "games-chessbase.pgn"}) {
        std::ifstream in(dir / name, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::size_t tags = 0;
        for (auto pos = text.find("Elo \""); pos != std::string::npos; pos = text.find("Elo \"", pos + 1)) ++tags;
        CHECK(tags == 12);
        CHECK(text.find("\"]\n[WhiteElo \"3500\"]\n") != std::string::npos);
        const auto white = text.find("[White \"random0\"]");
        REQUIRE(white != std::string::npos);
        CHECK(text.find("[WhiteElo \"3500\"]", white) > text.find("[Result ", white));
    }
    state.reset();
    CHECK(TournamentState::open(dir)->spec().participants[0].elo == 3500);
}

TEST_CASE("runner: a match stopped abruptly resumes to exactly its number of games") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available, skipped");
        return;
    }

    const auto dir = fs::temp_directory_path() / "fcg-tour-runner-resume";
    fs::remove_all(dir);

    {
        auto state = TournamentState::create(dir, spec(Format::Match, 2, 1, 60));
        CHECK(play(*state, 9).empty());
        CHECK_FALSE(state->finished());
        CHECK(state->results().size() >= 9);
        CHECK(state->results().size() < 60);
    }

    // Twice, to resume a resumed tournament too.
    {
        auto state = TournamentState::open(dir);
        CHECK(play(*state, state->results().size() + 7).empty());
    }

    auto state = TournamentState::open(dir);
    CHECK(play(*state).empty());
    CHECK(state->finished());

    const auto results = state->results();
    CHECK(results.size() == 60);
    std::set<uint64_t> numbers;
    for (const auto& r : results) numbers.insert(r.number());
    CHECK(numbers.size() == 60);
    CHECK(*numbers.rbegin() == 59);

    // Complete pairs only, both colours of each opening.
    const auto stats = state->pairStats(0, 1);
    CHECK(stats.sum() == 60);
    CHECK(stats.penta_WW + stats.penta_WD + stats.penta_WL + stats.penta_DD + stats.penta_LD + stats.penta_LL == 30);

    CHECK(countGames(dir / "games.pgn") == 60);
}

TEST_CASE("runner: a game whose pair partner was played before leaves no pair open in the pause gate") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available, skipped");
        return;
    }

    const auto dir = fs::temp_directory_path() / "fcg-tour-runner-lone";
    fs::remove_all(dir);
    auto state = TournamentState::create(dir, spec(Format::Match, 2, 1, 2));

    // Game 1 of the only encounter, as if played before a stop; the runner plays game 2 alone.
    const auto first = state->claimNext();
    REQUIRE(first);
    fastchess::MatchData data(chess::constants::STARTPOS);
    data.players     = {fastchess::MatchData::PlayerInfo(first->white, chess::GameResult::DRAW),
                        fastchess::MatchData::PlayerInfo(first->black, chess::GameResult::DRAW)};
    data.termination = fastchess::MatchTermination::NORMAL;
    data.duration    = "00:00:01";
    state->complete(*first, data);

    CHECK(play(*state).empty());
    CHECK(state->finished());
    // "Stop when idle" waits for open pairs: one left open would never let it stop.
    CHECK(fastchess::observer::PauseGate::instance().openPairs() == 0);
}

#ifndef _WIN32
TEST_CASE("runner: an engine that fails to start once is tried again, and the tournament goes on") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available, skipped");
        return;
    }

    const auto dir = fs::temp_directory_path() / "fcg-tour-runner-retry";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // Exits at once the first time it is started, then is the random mover.
    const auto script = dir / "flaky.sh";
    {
        std::ofstream out(script);
        out << "#!/bin/sh\nif [ ! -e \"" << (dir / "started").string() << "\" ]; then touch \""
            << (dir / "started").string() << "\"; exit 1; fi\nexec \"" << randomMover() << "\"\n";
    }
    fs::permissions(script, fs::perms::owner_all);

    auto s                       = spec(Format::Match, 2, 1, 2);
    s.conditions.concurrency     = 1;
    s.participants[1].engine.cmd = script.string();
    auto state                   = TournamentState::create(dir / "t", s);
    CHECK(play(*state).empty());
    CHECK_FALSE(fastchess::atomic::abnormal_termination);
    CHECK(state->finished());
    CHECK(state->results().size() == 2u);
}

TEST_CASE("runner: a game whose engine fails to start is given up quietly once its participant is withdrawn") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available, skipped");
        return;
    }

    const auto dir = fs::temp_directory_path() / "fcg-tour-runner-withdrawn";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // Never starts; tells that it was tried.
    const auto started = dir / "started";
    const auto script  = dir / "broken.sh";
    {
        std::ofstream out(script);
        out << "#!/bin/sh\ntouch \"" << started.string() << "\"\nexit 1\n";
    }
    fs::permissions(script, fs::perms::owner_all);

    auto s                       = spec(Format::RoundRobin, 3, 1);
    s.conditions.concurrency     = 1;
    s.participants[2].engine.cmd = script.string();
    auto state                   = TournamentState::create(dir / "t", s);

    // Withdrawn during the pause before its engine is tried again.
    std::thread withdraw([&] {
        while (!fs::exists(started)) std::this_thread::sleep_for(10ms);
        Edit edit;
        edit.kind        = Edit::Kind::Withdraw;
        edit.participant = 2;
        state->applyEdit(edit);
    });
    const auto begun = std::chrono::steady_clock::now();
    const auto error = play(*state);
    withdraw.join();

    CHECK(error.empty());
    CHECK_FALSE(fastchess::atomic::abnormal_termination);
    CHECK(state->finished());
    CHECK(state->results().size() == 2u);  // the games of 0 and 1
    CHECK(std::chrono::steady_clock::now() - begun < fastchess::BaseTournament::ENGINE_START_RETRY_PAUSE);
}

TEST_CASE("runner: a pause holds the next try of an engine that failed to start, without using it up") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available, skipped");
        return;
    }

    const auto dir = fs::temp_directory_path() / "fcg-tour-runner-retry-pause";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // Exits at once the first time it is started, then is the random mover.
    const auto started = dir / "started";
    const auto script  = dir / "flaky.sh";
    {
        std::ofstream out(script);
        out << "#!/bin/sh\nif [ ! -e \"" << started.string() << "\" ]; then touch \"" << started.string()
            << "\"; exit 1; fi\nexec \"" << randomMover() << "\"\n";
    }
    fs::permissions(script, fs::perms::owner_all);

    auto s                       = spec(Format::Match, 2, 1, 2);
    s.conditions.concurrency     = 1;
    s.participants[1].engine.cmd = script.string();
    auto state                   = TournamentState::create(dir / "t", s);

    // Paused while the engine waits for its next try: nothing is played during the pause, the try comes after it.
    constexpr auto held = 3s;
    std::thread pauser([&] {
        while (!fs::exists(started)) std::this_thread::sleep_for(10ms);
        fastchess::observer::PauseGate::instance().setPaused(true);
        std::this_thread::sleep_for(held);
        CHECK(state->results().empty());
        fastchess::observer::PauseGate::instance().setPaused(false);
    });
    const auto begun = std::chrono::steady_clock::now();
    const auto error = play(*state);
    pauser.join();

    CHECK(error.empty());
    CHECK_FALSE(fastchess::atomic::abnormal_termination);
    CHECK(state->finished());
    CHECK(state->results().size() == 2u);
    CHECK(std::chrono::steady_clock::now() - begun >= fastchess::BaseTournament::ENGINE_START_RETRY_PAUSE + held);
}

TEST_CASE("runner: \"stop when idle\" gives up at once a game whose engine waits for its next try") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available, skipped");
        return;
    }

    const auto dir = fs::temp_directory_path() / "fcg-tour-runner-retry-idle";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // Never starts; tells that it was tried.
    const auto started = dir / "started";
    const auto script  = dir / "broken.sh";
    {
        std::ofstream out(script);
        out << "#!/bin/sh\ntouch \"" << started.string() << "\"\nexit 1\n";
    }
    fs::permissions(script, fs::perms::owner_all);

    auto s                       = spec(Format::Match, 2, 1, 2);
    s.conditions.concurrency     = 1;
    s.participants[1].engine.cmd = script.string();
    auto state                   = TournamentState::create(dir / "t", s);

    // As the GUI does: stops once no game runs and no pair waits for its second game.
    std::thread stopper([&] {
        while (!fs::exists(started)) std::this_thread::sleep_for(10ms);
        auto& gate = fastchess::observer::PauseGate::instance();
        gate.stopWhenIdle();
        while (gate.running() > 0 || gate.openPairs() > 0) std::this_thread::sleep_for(10ms);
        fastchess::atomic::stop = true;
    });
    const auto begun = std::chrono::steady_clock::now();
    const auto error = play(*state);
    stopper.join();

    CHECK(error.empty());
    CHECK_FALSE(fastchess::atomic::abnormal_termination);
    CHECK(state->results().empty());
    CHECK(std::chrono::steady_clock::now() - begun < fastchess::BaseTournament::ENGINE_START_RETRY_PAUSE);
}

TEST_CASE("runner: a round robin of more engines than games at a time starts each engine once") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available, skipped");
        return;
    }

    const auto dir = fs::temp_directory_path() / "fcg-tour-runner-starts";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // The random mover, writing a line to `starts` each time it is started.
    const auto starts = dir / "starts";
    const auto script = dir / "counted.sh";
    {
        std::ofstream out(script);
        out << "#!/bin/sh\necho started >> \"" << starts.string() << "\"\nexec \"" << randomMover() << "\"\n";
    }
    fs::permissions(script, fs::perms::owner_all);

    auto s                   = spec(Format::RoundRobin, 7, 1);
    s.conditions.concurrency = 1;
    for (auto& p : s.participants) p.engine.cmd = script.string();
    auto state = TournamentState::create(dir / "t", s);
    CHECK(play(*state).empty());
    CHECK(state->results().size() == 42u);

    std::ifstream in(starts);
    std::size_t lines = 0;
    for (std::string line; std::getline(in, line);) ++lines;
    CHECK(lines == 7u);
}
#endif

TEST_CASE("runner: engine logs, of every game when asked, with both directions of both engines") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available, skipped");
        return;
    }

    const auto dir = fs::temp_directory_path() / "fcg-tour-runner-logs";
    fs::remove_all(dir);
    auto s           = spec(Format::Match, 2, 1, 4);
    s.log_every_game = true;
    auto state       = TournamentState::create(dir, s);
    CHECK(play(*state).empty());
    REQUIRE(state->results().size() == 4u);

    for (uint64_t n = 1; n <= 4; ++n) {
        std::ifstream in(engineLogPath(dir, n), std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(text.find(fmt::format("engine log, game {}", n)) != std::string::npos);
        CHECK(text.find("W> ucinewgame") != std::string::npos);
        CHECK(text.find("B> go ") != std::string::npos);
        CHECK(text.find("W< bestmove ") != std::string::npos);
    }
}

TEST_CASE("runner: engine logs keep the engines' error output, marked apart, even more than a pipe holds") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available, skipped");
        return;
    }

    const auto dir = fs::temp_directory_path() / "fcg-tour-runner-stderr";
    fs::remove_all(dir);
    auto s           = spec(Format::Match, 2, 1, 2);
    s.log_every_game = true;
    auto state       = TournamentState::create(dir, s);

    setEnv("FCG_MOCK_STDERR", "on stderr:");
    // About 100 KB on each engine's error output before it answers "uci", more than a pipe holds (64 KB on Linux,
    // 4 KB on Windows), in few enough lines that both engines' fit in the first lines of the log: the middle of a
    // long game's log is left out.
    setEnv("FCG_MOCK_STDERR_FLOOD", "100");
    const auto error = play(*state);
    setEnv("FCG_MOCK_STDERR", nullptr);
    setEnv("FCG_MOCK_STDERR_FLOOD", nullptr);

    CHECK(error.empty());
    REQUIRE(state->results().size() == 2u);

    // Both engines started for the first game.
    const auto text = readFile(engineLogPath(dir, 1));
    CHECK(text.find("! read from its error output") != std::string::npos);
    CHECK(text.find("W! flood ...") != std::string::npos);
    CHECK(text.find("B! flood ...") != std::string::npos);
    // None lost.
    CHECK(countOf(text, "W! flood ...") == 100u);
    CHECK(countOf(text, "B! flood ...") == 100u);
    CHECK(text.find("W! on stderr: ") != std::string::npos);
    CHECK(text.find("B! on stderr: ") != std::string::npos);
    CHECK(text.find("< on stderr: ") == std::string::npos);
    CHECK(text.find("W< bestmove ") != std::string::npos);
}

TEST_CASE("runner: an engine's last words on its error output, before it ends, are all in the engine log") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available, skipped");
        return;
    }

    const auto dir = fs::temp_directory_path() / "fcg-tour-runner-last-words";
    fs::remove_all(dir);
    auto s           = spec(Format::Match, 2, 1, 1);
    s.log_every_game = true;
    auto state       = TournamentState::create(dir, s);

    // White's engine writes about 50 KB on its error output at its first move, at once, and ends: its end is seen
    // when most of it is still to read.
    setEnv("FCG_MOCK_STDERR_CRASH", "50");
    (void)play(*state);
    setEnv("FCG_MOCK_STDERR_CRASH", nullptr);

    REQUIRE_FALSE(state->results().empty());
    const auto text = readFile(engineLogPath(dir, 1));
    CHECK(text.find("W< info string crashing") != std::string::npos);
    CHECK(countOf(text, "W! crash report ") == 50u);
    CHECK(text.find("W! crash report 50/50 ") != std::string::npos);
}

namespace {

// The random mover, started with `variable` set to `value` in its environment, its error output read into `errors`.
struct MockProcess {
    MockProcess(const char* variable, const char* value) {
        fastchess::engine::process::capture_stderr = true;
        fastchess::atomic::stop                    = false;  // left set by the games played before
        process.setErrorLineCallback([this](const std::string& line) { errors.push_back(line); });
        setEnv(variable, value);
        started = process.init(fs::path(randomMover()).parent_path().string(), randomMover(), "", "mock").code ==
                  fastchess::engine::process::Status::OK;
        setEnv(variable, nullptr);
        process.setupRead();
    }
    // Ended at once, rather than after the minute an engine is given to end.
    ~MockProcess() {
        if (started) (void)process.writeInput("quit\n");
    }
    MockProcess(const MockProcess&)            = delete;
    MockProcess& operator=(const MockProcess&) = delete;

    fastchess::engine::process::Process process;
    std::vector<std::string> errors;
    bool started = false;
};

}  // namespace

TEST_CASE("engine process: what an engine wrote on its error output before it ended is read before its end") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available, skipped");
        return;
    }
    using fastchess::engine::process::Status;

    // About 50 KB on its error output at once, then the engine ends, all before anything is read: its end (that
    // of its output) and its last words are there together.
    MockProcess mock("FCG_MOCK_STDERR_CRASH", "50");
    REQUIRE(mock.started);
    CHECK(mock.process.writeInput("go\n").code == Status::OK);
    std::this_thread::sleep_for(500ms);

    std::vector<fastchess::engine::process::Line> lines;
    const auto begun = std::chrono::steady_clock::now();
    CHECK(mock.process.readOutput(lines, "bestmove", 5000ms).code == Status::ERR);
    CHECK(std::chrono::steady_clock::now() - begun < 2s);
    REQUIRE(mock.errors.size() == 50u);
    CHECK(mock.errors.front().rfind("crash report 1/50 ", 0) == 0);
    CHECK(mock.errors.back().rfind("crash report 50/50 ", 0) == 0);
    CHECK(std::count_if(lines.begin(), lines.end(),
                        [](const auto& line) { return line.std == fastchess::engine::process::Standard::ERR; }) == 50);
}

TEST_CASE("engine process: what an engine wrote on its error output before the line awaited comes with it") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available, skipped");
        return;
    }
    using fastchess::engine::process::Status;

    // "on stderr: <move> ..." longer than a read takes at once, then its bestmove, all before anything is read:
    // the read ends on the bestmove, which may be the last move of a game, and the engine's next read is in the
    // next game.
    setEnv("FCG_MOCK_STDERR_PAD", "6000");
    MockProcess mock("FCG_MOCK_STDERR", "on stderr:");
    setEnv("FCG_MOCK_STDERR_PAD", nullptr);
    REQUIRE(mock.started);
    CHECK(mock.process.writeInput("go\n").code == Status::OK);
    std::this_thread::sleep_for(500ms);

    std::vector<fastchess::engine::process::Line> lines;
    REQUIRE(mock.process.readOutput(lines, "bestmove", 5000ms).code == Status::OK);
    REQUIRE(mock.errors.size() == 1u);
    CHECK(mock.errors.front().rfind("on stderr: ", 0) == 0);
    CHECK(mock.errors.front().size() > 6000u);
}

TEST_CASE("engine cache: the idle entries used the longest ago make room; retired ones go at once") {
    struct Engine {
        explicit Engine(int* alive) : alive_(alive) { ++*alive_; }
        ~Engine() { --*alive_; }
        int* alive_;
    };
    int alive = 0;
    fastchess::util::CachePool<Engine, std::string> cache(3);

    const auto use = [&](const std::string& name) {
        auto it = cache.getEntry(name, &alive);
        fastchess::util::ScopeGuard guard(&*it);
    };
    use("a");
    use("b");
    use("c");
    CHECK(alive == 3);
    use("a");  // reused, and now the most recent
    use("d");  // b goes
    CHECK(alive == 3);
    use("a");
    use("c");
    CHECK(alive == 3);
    use("b");  // d goes, a and c stay
    CHECK(alive == 3);

    // Entries in use are never taken away, even past the limit.
    {
        auto e1 = cache.getEntry("e", &alive);
        auto e2 = cache.getEntry("e", &alive);
        auto e3 = cache.getEntry("e", &alive);
        auto e4 = cache.getEntry("e", &alive);
        CHECK(alive == 4);
        fastchess::util::ScopeGuard g1(&*e1), g2(&*e2), g3(&*e3), g4(&*e4);
    }

    // Handed over to be destroyed outside the pool's lock.
    auto retired = cache.evictIdle([](const std::string& name) { return name == "e"; });
    CHECK(alive == 4);
    retired.clear();
    CHECK(alive == 0);
}

TEST_CASE("engine cache: one idle process per engine left in moderate fields, a cap in big ones") {
    using fastchess::BaseTournament;
    constexpr auto cap = BaseTournament::MAX_IDLE_ENGINES;

    // A round robin of 10 at concurrency 1: the two playing, and every engine kept for its next game.
    CHECK(BaseTournament::engineCacheSize(10, 1, 1) == 12u);
    // At concurrency 8, never fewer idle processes than in 0.4.11 (two per game).
    CHECK(BaseTournament::engineCacheSize(10, 8, 1) == 32u);
    // Hundreds of engines: the cap.
    CHECK(BaseTournament::engineCacheSize(500, 1, 1) == 2 + cap);
    CHECK(BaseTournament::engineCacheSize(500, 4, 1) == 8 + cap);
    // With affinity, a cache per game thread shares the cap with the others.
    CHECK(BaseTournament::engineCacheSize(10, 1, 4) == 2 + cap / 4);
    CHECK(BaseTournament::engineCacheSize(500, 1, 64) == 4u);
    CHECK(BaseTournament::engineCacheSize(0, 0, 0) == 4u);

    // A new size applies from the next entry made.
    struct Engine {
        explicit Engine(int* alive) : alive_(alive) { ++*alive_; }
        ~Engine() { --*alive_; }
        int* alive_;
    };
    int alive = 0;
    fastchess::util::CachePool<Engine, std::string> cache(2);
    const auto use = [&](const std::string& name) {
        auto it = cache.getEntry(name, &alive);
        fastchess::util::ScopeGuard guard(&*it);
    };
    use("a");
    use("b");
    use("c");
    CHECK(alive == 2);
    cache.setMaxSize(3);
    use("a");
    CHECK(alive == 3);
    cache.setMaxSize(1);
    CHECK(alive == 3);
    use("d");
    CHECK(alive == 1);
}

TEST_CASE("engine log: the first and the last lines of a long game; which endings keep it") {
    EngineLog log;
    log.start();
    const auto total = EngineLog::HEAD_LINES + EngineLog::TAIL_LINES + 10;
    for (std::size_t i = 0; i < total; ++i) {
        log.add(chess::Color::WHITE, i % 2 == 0 ? EngineLog::Stream::Input : EngineLog::Stream::Output,
                fmt::format("line {}", i));
    }
    fastchess::MatchData data("startpos");
    const auto text = log.text(7, data);
    CHECK(text.find("W> line 0\n") != std::string::npos);
    CHECK(text.find("... 10 lines left out ...") != std::string::npos);
    CHECK(text.find(fmt::format("line {}\n", EngineLog::HEAD_LINES + 5)) == std::string::npos);
    CHECK(text.find(fmt::format("line {}\n", total - 1)) != std::string::npos);

    CHECK(endsBadly(fastchess::MatchTermination::TIMEOUT));
    CHECK(endsBadly(fastchess::MatchTermination::DISCONNECT));
    CHECK(endsBadly(fastchess::MatchTermination::ILLEGAL_MOVE));
    CHECK_FALSE(endsBadly(fastchess::MatchTermination::NORMAL));
    CHECK_FALSE(endsBadly(fastchess::MatchTermination::ADJUDICATION));
}
