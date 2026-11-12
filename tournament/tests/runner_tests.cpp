// Real games with fastchess' random mover engine, through the Runner.

#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <thread>

#include <core/config/config.hpp>
#include <core/globals/globals.hpp>
#include <core/memory/cache.hpp>
#include <core/observer.hpp>

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

    cache.evictIdle([](const std::string& name) { return name == "e"; });
    CHECK(alive == 0);
}

TEST_CASE("engine log: the first and the last lines of a long game; which endings keep it") {
    EngineLog log;
    log.start();
    const auto total = EngineLog::HEAD_LINES + EngineLog::TAIL_LINES + 10;
    for (std::size_t i = 0; i < total; ++i) log.add(chess::Color::WHITE, i % 2 == 0, fmt::format("line {}", i));
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
