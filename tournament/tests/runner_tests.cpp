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
    auto state = TournamentState::create(dir, spec(Format::RoundRobin, 4, 2));

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
