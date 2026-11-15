// What games take of the machine, how the window shares it between tournaments, and the runner's allowance.

#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

#include <core/config/config.hpp>
#include <core/globals/globals.hpp>
#include <core/observer.hpp>

#include <tour/budget.hpp>
#include <tour/runner.hpp>

#include "self_path.hpp"

#ifdef _WIN32
#    include <windows.h>
#else
#    include <cstdlib>
#endif

using namespace fcg::tour;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

Participant participant(int id, int threads = 1, int hash = 0, bool gpu = false) {
    Participant p;
    p.id          = id;
    p.engine.name = "e" + std::to_string(id);
    if (threads > 1) p.engine.options.emplace_back("Threads", std::to_string(threads));
    if (hash > 0) p.engine.options.emplace_back("Hash", std::to_string(hash));
    p.gpu = gpu;
    return p;
}

Claim claim(int threads, int ceiling, double weight = 1, bool active = true) {
    Claim c;
    c.game.threads = threads;
    c.ceiling      = ceiling;
    c.weight       = weight;
    c.active       = active;
    return c;
}

}  // namespace

TEST_CASE("budget: a game takes its two engines' threads, their Hash and a process allowance, and their GPUs") {
    const auto a = participant(0, 4, 256, true);
    const auto b = participant(1);
    CHECK(engineThreads(a.engine) == 4);
    CHECK(engineThreads(b.engine) == 1);
    CHECK(engineHashMb(a.engine) == 256);
    CHECK(engineHashMb(b.engine) == DEFAULT_HASH_MB);

    // One engine thinks while the other waits: the larger Threads; both are in memory.
    const auto cost = gameCost(a, b);
    CHECK(cost.threads == 4);
    CHECK(cost.memory_mb == 256 + DEFAULT_HASH_MB + 2 * PROCESS_MB + 5 * THREAD_MB);
    CHECK(cost.gpus == 1);

    // The heaviest game of a tournament: its two heaviest active participants.
    TournamentSpec spec;
    spec.participants           = {participant(0, 2), participant(1, 8, 1024), participant(2, 4), participant(3, 16)};
    spec.participants[3].active = false;  // withdrawn: plays no more
    const auto largest          = largestGame(spec);
    CHECK(largest.threads == 8);
    CHECK(largest.memory_mb == processMb(spec.participants[1].engine) + processMb(spec.participants[2].engine));
    CHECK(largest.gpus == 0);
}

TEST_CASE("budget: an allowance admits a game when it fits with those in flight, one game always when allowed") {
    Allowance a;
    a.threads   = 4;
    a.memory_mb = 1000;
    a.gpus      = 1;

    GameCost game{2, 300, 0};
    CHECK(a.admits({}, 0, game) == Wait::None);
    CHECK(a.admits({2, 300, 0}, 1, game) == Wait::None);
    CHECK(a.admits({4, 600, 0}, 2, game) == Wait::Cores);
    CHECK(a.admits({2, 800, 0}, 1, game) == Wait::Memory);

    // A GPU game waits for the GPU held by another; none shares it.
    const GameCost gpu_game{2, 300, 1};
    CHECK(a.admits({2, 300, 1}, 1, gpu_game) == Wait::Gpus);
    CHECK(a.admits({2, 300, 0}, 1, gpu_game) == Wait::None);

    // Heavier than the whole allowance: it plays alone when one game is always allowed, not otherwise.
    const GameCost heavy{8, 2000, 0};
    CHECK(a.admits({}, 0, heavy) == Wait::None);
    a.one_game_always = false;
    CHECK(a.admits({}, 0, heavy) == Wait::Cores);

    // No allowance at all: nothing starts.
    Allowance none;
    none.threads         = 0;
    none.one_game_always = false;
    CHECK(none.admits({}, 0, game) == Wait::Cores);
    // No limit.
    CHECK(Allowance{}.admits({1000, 100000, 50}, 400, heavy) == Wait::None);
}

TEST_CASE("budget: the cores are shared by weight, each tournament up to its concurrency, the rest to the others") {
    Budget budget;
    budget.threads = 16;

    SUBCASE("equal weights, equal halves") {
        const auto out = share(budget, {claim(2, 16), claim(2, 16)});
        CHECK(out[0].threads == 8);
        CHECK(out[1].threads == 8);
        CHECK(out[0].one_game_always);
        CHECK_FALSE(out[1].one_game_always);
    }
    SUBCASE("weights 1 and 3: a quarter and three quarters") {
        const auto out = share(budget, {claim(2, 16, 1), claim(2, 16, 3)});
        CHECK(out[0].threads == 4);
        CHECK(out[1].threads == 12);
    }
    SUBCASE("a tournament below its share at its concurrency leaves the rest to the other") {
        const auto out = share(budget, {claim(2, 2), claim(2, 16)});
        CHECK(out[0].threads == 4);
        CHECK(out[1].threads == 12);
    }
    SUBCASE("a paused tournament gets nothing") {
        const auto out = share(budget, {claim(2, 16, 5, false), claim(2, 4)});
        CHECK(out[0].threads == 0);
        CHECK_FALSE(out[0].one_game_always);
        CHECK(out[1].threads == 8);
        CHECK(out[1].one_game_always);
    }
    SUBCASE("one game's worth each while the budget allows, whatever the weights") {
        const auto out = share(budget, {claim(4, 8, 100), claim(4, 8, 1), claim(4, 8, 1)});
        CHECK(out[1].threads >= 4);
        CHECK(out[2].threads >= 4);
        CHECK(out[0].threads + out[1].threads + out[2].threads == 16);
    }
    SUBCASE("a budget smaller than a game: the first plays it alone, the others wait") {
        budget.threads = 6;
        const auto out = share(budget, {claim(8, 4), claim(8, 4)});
        CHECK(out[0].threads == 8);
        CHECK(out[0].one_game_always);
        CHECK(out[1].threads == 0);
        Allowance second = out[1];
        CHECK(second.admits({}, 0, {8, 0, 0}) == Wait::Cores);
    }
    SUBCASE("threads no whole game takes go to a tournament below its ceiling") {
        budget.threads = 15;
        const auto out = share(budget, {claim(2, 16)});
        CHECK(out[0].threads == 15);
    }
}

TEST_CASE("budget: memory and GPUs are shared like the cores") {
    Budget budget;
    budget.threads   = 32;
    budget.memory_mb = 1000;
    budget.gpus      = 1;

    auto a           = claim(2, 8);
    a.game.memory_mb = 400;
    auto b           = a;
    auto out         = share(budget, {a, b});
    // Two games of 400 MB fit in 1000 MB: one each.
    CHECK(out[0].memory_mb == 400);
    CHECK(out[1].memory_mb == 400);
    CHECK(out[0].threads >= 2);
    CHECK(out[1].threads >= 2);
    CHECK(out[0].gpus == 0);
    // Memory for one game each: the second waits for it.
    CHECK(Allowance(out[0]).admits({2, 400, 0}, 1, a.game) == Wait::Memory);

    // One GPU: the first tournament whose games need one has it, the other waits.
    a.game.gpus = b.game.gpus = 1;
    out                       = share(budget, {a, b});
    CHECK(out[0].gpus == 1);
    CHECK(out[1].threads == 0);

    // No memory or GPU limit in the budget: none in the allowances.
    out = share(Budget{8, 0, 0}, {a});
    CHECK(out[0].memory_mb == -1);
    CHECK(out[0].gpus == -1);

    // What the setup summary says a budget allows.
    CHECK(gamesAllowed(Budget{16, 0, 0}, {2, 100, 0}, 16) == 8);
    CHECK(gamesAllowed(Budget{16, 1000, 0}, {2, 400, 0}, 16) == 2);
    CHECK(gamesAllowed(Budget{16, 0, 2}, {2, 100, 1}, 16) == 2);
    CHECK(gamesAllowed(Budget{4, 0, 0}, {8, 100, 0}, 3) == 1);
}

// --- the runner
// -------------------------------------------------------------------------------------------------------

namespace {

std::string randomMover() { return (fcg::test::selfPath().parent_path() / FCG_RANDOM_MOVER).string(); }

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

TournamentSpec randomSpec(Format format, int players, int concurrency, uint64_t max_games = 0) {
    TournamentSpec s;
    s.name                   = "budget test";
    s.format                 = format;
    s.cycles                 = 1;
    s.max_games              = max_games;
    s.conditions.concurrency = concurrency;
    s.conditions.recover     = true;
    for (int i = 0; i < players; ++i) {
        auto p               = participant(i);
        p.engine.name        = "random" + std::to_string(i);
        p.engine.cmd         = randomMover();
        p.engine.limit.nodes = 1;
        s.participants.push_back(p);
    }
    return s;
}

void configure(const TournamentState& state) {
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
}

// Counts the games being played, the most at once, and the games of `gpu_engine` at once.
struct Concurrency final : fastchess::observer::GameObserver {
    void onGameStart(const fastchess::observer::GameStart& info) override {
        std::lock_guard<std::mutex> lock(mutex);
        ++running;
        ++started;
        most = std::max(most, running);
        if (settled) most_settled = std::max(most_settled, running);
        const bool gpu = info.white == gpu_engine || info.black == gpu_engine;
        if (gpu) gpus_running++;
        most_gpus            = std::max(most_gpus, gpus_running);
        gpu_slots[info.slot] = gpu;
    }
    void onGameEnd(int slot, std::size_t, std::size_t, const fastchess::MatchData&, bool counted) override {
        std::lock_guard<std::mutex> lock(mutex);
        --running;
        if (!counted) ++interrupted;
        if (gpu_slots[slot]) gpus_running--;
        gpu_slots[slot] = false;
        if (lowered && running <= 1) settled = true;
    }
    int now() {
        std::lock_guard<std::mutex> lock(mutex);
        return running;
    }

    std::mutex mutex;
    int running = 0, most = 0, started = 0, interrupted = 0;
    bool lowered = false, settled = false;
    int most_settled = 0;
    std::string gpu_engine;
    int gpus_running = 0, most_gpus = 0;
    std::map<int, bool> gpu_slots;
};

template <typename F>
bool waitUntil(F done, std::chrono::milliseconds timeout = 30s) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end) {
        if (done()) return true;
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

}  // namespace

TEST_CASE("runner: an allowance changed while it plays holds games back or starts them, never interrupting one") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available, skipped");
        return;
    }
    const auto dir = fs::temp_directory_path() / "fcg-tour-budget-runner";
    fs::remove_all(dir);
    auto state = TournamentState::create(dir, randomSpec(Format::Match, 2, 4, 36));
    configure(*state);
    setEnv("FCG_MOCK_MOVE_MS", "2");

    Concurrency watch;
    Runner runner(*state, &watch, nullptr);
    // Single-thread engines: a game takes 1 thread, one game at a time.
    Allowance one;
    one.threads         = 1;
    one.one_game_always = false;
    runner.setAllowance(one);
    std::thread thread([&] { runner.start(); });

    REQUIRE(waitUntil([&] { return state->results().size() >= 3; }));
    CHECK(watch.most == 1);
    CHECK(waitUntil([&] { return runner.waiting() == Wait::Cores; }, 5s));

    // Room for four games: they start at once.
    Allowance four = one;
    four.threads   = 4;
    runner.setAllowance(four);
    CHECK(waitUntil([&] { return watch.now() >= 3; }));
    CHECK(runner.inFlight().threads <= 4);

    // Back to one: the games in flight end as they go, and none starts until one game is left.
    {
        std::lock_guard<std::mutex> lock(watch.mutex);
        watch.lowered = true;
    }
    runner.setAllowance(one);
    thread.join();
    setEnv("FCG_MOCK_MOVE_MS", nullptr);

    CHECK(runner.error().empty());
    CHECK(state->finished());
    CHECK(state->results().size() == 36);
    CHECK(watch.interrupted == 0);
    CHECK(watch.most >= 3);
    CHECK(watch.settled);
    CHECK(watch.most_settled <= 1);
    fastchess::stopProcesses();
}

TEST_CASE("runner: games wait for memory, and no more GPU engines play at once than the allowance's GPUs") {
    if (!fs::is_regular_file(randomMover())) {
        MESSAGE("random mover not available, skipped");
        return;
    }
    setEnv("FCG_MOCK_MOVE_MS", "1");

    SUBCASE("memory") {
        const auto dir = fs::temp_directory_path() / "fcg-tour-budget-memory";
        fs::remove_all(dir);
        auto state = TournamentState::create(dir, randomSpec(Format::Match, 2, 3, 8));
        configure(*state);
        Concurrency watch;
        Runner runner(*state, &watch, nullptr);
        // One game takes 2 x (16 + 64 + 8) MB: two do not fit in 300.
        Allowance a;
        a.memory_mb = 300;
        runner.setAllowance(a);
        std::atomic_bool waited{false};
        std::atomic_bool done{false};
        std::thread poll([&] {
            while (!done) {
                if (runner.waiting() == Wait::Memory) waited = true;
                std::this_thread::sleep_for(1ms);
            }
        });
        runner.start();
        done = true;
        poll.join();
        CHECK(state->results().size() == 8);
        CHECK(watch.most == 1);
        CHECK(waited);
        fastchess::stopProcesses();
    }
    SUBCASE("GPUs") {
        const auto dir = fs::temp_directory_path() / "fcg-tour-budget-gpu";
        fs::remove_all(dir);
        auto spec                = randomSpec(Format::RoundRobin, 4, 4);
        spec.cycles              = 2;
        spec.participants[0].gpu = true;
        auto state               = TournamentState::create(dir, spec);
        configure(*state);
        Concurrency watch;
        watch.gpu_engine = "random0";
        Runner runner(*state, &watch, nullptr);
        Allowance a;
        a.gpus = 1;
        runner.setAllowance(a);
        runner.start();
        CHECK(state->finished());
        CHECK(watch.most_gpus == 1);
        // The other games play beside it.
        CHECK(watch.most >= 2);
        fastchess::stopProcesses();
    }
    setEnv("FCG_MOCK_MOVE_MS", nullptr);
}
