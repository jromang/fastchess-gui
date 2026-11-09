#include "headless.hpp"

#include <csignal>
#include <cstdio>
#include <mutex>

#include <fmt/include/fmt/core.h>
#include <core/globals/globals.hpp>
#include <matchmaking/elo/elo_pentanomial.hpp>
#include <tour/standings.hpp>

#include "run.hpp"

namespace fcg {

namespace {

void onSignal(int) { fastchess::atomic::stop = true; }

// Prints each recorded game, and the standings (or the match statistics) now and then.
class ConsoleListener final : public tour::RecordListener {
   public:
    explicit ConsoleListener(const tour::TournamentState& state) : state_(state) {}

    void onRecorded(const tour::GameResult& result, const fastchess::MatchData&,
                    const std::optional<fastchess::Stats>&) override {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto& spec  = state_.spec();
        const auto name   = [&](int id) { return spec.participant(id) ? spec.participant(id)->engine.name : "?"; };
        const auto total  = state_.totalGames();
        const auto played = ++played_ + already_;
        std::printf("%s  %s - %s  %s  {%s}\n",
                    total ? fmt::format("[{}/{}]", played, *total).c_str() : fmt::format("[{}]", played).c_str(),
                    name(result.white).c_str(), name(result.black).c_str(), result.result.c_str(),
                    result.reason.c_str());
        if (played_ % 20 == 0) printStatus();
        std::fflush(stdout);
    }

    void setAlready(uint64_t n) { already_ = n; }

    void printStatus() const {
        const auto& spec = state_.spec();
        if (spec.format == tour::Format::Match) {
            const auto stats = state_.pairStats(spec.participants[0].id, spec.participants[1].id);
            if (stats.sum() == 0) return;
            const fastchess::elo::EloPentanomial elo(stats);
            std::printf("--- %s vs %s: +%d =%d -%d, Elo %s\n", spec.participants[0].engine.name.c_str(),
                        spec.participants[1].engine.name.c_str(), stats.wins, stats.draws, stats.losses,
                        elo.getElo().c_str());
            return;
        }
        std::printf("\n%s\n", tour::tableText(tour::computeTable(spec, state_.results()), spec.name).c_str());
    }

   private:
    const tour::TournamentState& state_;
    std::mutex mutex_;
    uint64_t played_  = 0;
    uint64_t already_ = 0;
};

}  // namespace

int runHeadless(const std::string& target, const Storage& storage, const CpuTopology& topology) {
    std::unique_ptr<tour::TournamentState> state;
    try {
        if (tour::TournamentState::isTournament(target)) {
            state = tour::TournamentState::open(target);
        } else {
            // A setup or template file: a new tournament, with the engines of the profile.
            const auto setup = readJson<MatchSetup>(target);
            if (!setup) {
                std::fprintf(stderr, "%s is neither a tournament folder nor a setup file\n", target.c_str());
                return 2;
            }
            const auto engines = storage.loadEngines();
            const auto dir =
                storage.runsDir() /
                (setup->format == TournamentFormat::Match
                     ? makeRunDirName(setup->engine_a, setup->engine_b)
                     : makeTournamentDirName(setup->name.empty() ? defaultTournamentName(*setup) : setup->name));
            const auto run = buildRun(*setup, engines, topology, dir);
            state          = tour::TournamentState::create(dir, run.spec);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }

    for (const auto& w : state->warnings()) std::fprintf(stderr, "warning: %s\n", w.c_str());
    if (state->finished()) {
        std::printf("%s is already finished (%s).\n", state->dir().string().c_str(), state->finishReason().c_str());
        return 0;
    }

    std::printf("%s: %s\nFolder: %s\nCtrl+C stops it; it resumes where it stopped with --run on the folder.\n\n",
                state->spec().name.c_str(),
                state->results().empty() ? "starting"
                                         : fmt::format("resuming, {} games played", state->results().size()).c_str(),
                state->dir().string().c_str());

    ConsoleListener listener(*state);
    listener.setAlready(state->results().size());

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    configureFastchess(state->spec());
    const auto error = playTournament(*state, nullptr, &listener);

    listener.printStatus();
    if (!error.empty()) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    if (!state->finished()) {
        std::printf("Stopped. Resume with: fastchess-gui --run \"%s\"\n", state->dir().string().c_str());
        return 3;
    }
    std::printf("Finished (%s).\n", state->finishReason().c_str());
    return 0;
}

}  // namespace fcg
