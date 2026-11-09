#include "headless.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <mutex>
#include <thread>

#include <fmt/include/fmt/format.h>
#include <core/globals/globals.hpp>
#include <matchmaking/elo/elo_pentanomial.hpp>
#include <tour/standings.hpp>

#include "broadcaster.hpp"
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
        std::printf("\n%s\n",
                    tour::tableText(tour::computeTable(spec, state_.results(), std::nullopt, state_.byes()), spec.name)
                        .c_str());
    }

   private:
    const tour::TournamentState& state_;
    std::mutex mutex_;
    uint64_t played_  = 0;
    uint64_t already_ = 0;
};

}  // namespace

int runHeadless(const std::string& target, const Storage& storage, const CpuTopology& topology,
                const HeadlessBroadcast& broadcast) {
    std::unique_ptr<tour::TournamentState> state;
    BroadcastSettings settings;
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
            settings.files     = setup->broadcast_files;
            settings.dir       = setup->broadcast_dir;
            settings.tlcs      = setup->broadcast_tlcs;
            settings.tlcs_port = setup->tlcs_port;
            settings.web       = setup->broadcast_web;
            settings.web_port  = setup->web_port;
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

    // The command line comes before the setup file.
    if (broadcast.tlcs_port) settings.tlcs = true, settings.tlcs_port = *broadcast.tlcs_port;
    if (broadcast.web_port) settings.web = true, settings.web_port = *broadcast.web_port;
    if (broadcast.live_dir) settings.files = true, settings.dir = *broadcast.live_dir;
    const bool broadcasting = settings.files || settings.tlcs || settings.web;

    // Broadcasting needs the games in progress: the observer the window uses, read by a thread of its own.
    std::unique_ptr<GuiObserver> observer;
    Broadcaster broadcaster;
    std::atomic_bool playing{true};
    std::thread broadcast_thread;
    if (broadcasting) {
        for (const auto& error : broadcaster.configure(settings)) std::fprintf(stderr, "warning: %s\n", error.c_str());
        if (broadcaster.tlcs().running()) std::printf("TLCS server on UDP port %d\n", settings.tlcs_port);
        if (broadcaster.web().running()) std::printf("Web page on http://localhost:%d/\n", settings.web_port);
        if (settings.files) {
            std::printf("Live files in %s\n",
                        (settings.dir.empty() ? state->dir() / "live" : settings.dir).string().c_str());
        }
        std::printf("\n");

        observer         = std::make_unique<GuiObserver>(state->spec().conditions.concurrency, *state);
        broadcast_thread = std::thread([&] {
            std::vector<GameRecord> games;
            std::vector<fastchess::Stats> stats;
            while (playing) {
                broadcaster.update(*state, *observer, std::nullopt, -1);
                observer->drain(games, stats);  // the window's lists, not needed here
                games.clear();
                stats.clear();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            // The end of the last games, then time for the clients to acknowledge it.
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (std::chrono::steady_clock::now() < until) {
                broadcaster.update(*state, *observer, std::nullopt, -1);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            broadcaster.stop();
        });
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    configureFastchess(state->spec());
    const auto error = playTournament(*state, observer.get(), &listener);
    playing          = false;
    if (broadcast_thread.joinable()) broadcast_thread.join();

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
