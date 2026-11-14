#include "headless.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <mutex>
#include <optional>
#include <thread>

#include <fmt/include/fmt/format.h>
#include <core/globals/globals.hpp>
#include <matchmaking/elo/elo_pentanomial.hpp>
#include <tour/pgn_book.hpp>
#include <tour/standings.hpp>

#include "broadcaster.hpp"
#include "memory.hpp"
#include "run.hpp"

namespace fcg {

namespace {

void onSignal(int) { fastchess::atomic::stop = true; }

// The participants' PGN books turned into Polyglot books before the games (or found in the cache), their progress
// shown. False when one cannot be.
bool preparePgnBooks(const std::vector<OwnBookNeed>& needs) {
    for (const auto& need : needs) {
        const auto file                        = std::filesystem::path(need.path).filename().string();
        std::optional<tour::PreparedBook> book = tour::cachedPgnBook(need.path, need.depth, need.chess960);
        if (!book) {
            std::printf("Preparing the book of %s from %s, %d plies of its games: ", need.participant.c_str(),
                        file.c_str(), tour::pgnBookPlies(need.depth));
            std::fflush(stdout);
            int shown = 0;
            try {
                book = tour::preparePgnBook(need.path, need.depth, need.chess960, [&](double share) {
                    for (; shown + 10 <= static_cast<int>(share * 100); std::fflush(stdout)) {
                        shown += 10;
                        std::printf("%d%% ", shown);
                    }
                    return true;
                });
            } catch (const std::exception& e) {
                std::printf("\n");
                std::fprintf(stderr, "error: the book of %s cannot be used: %s\n", need.participant.c_str(), e.what());
                return false;
            }
            std::printf("done: %llu games, %llu positions.\n", static_cast<unsigned long long>(book->stats.games),
                        static_cast<unsigned long long>(book->stats.positions));
        }
        if (book->stats.skipped > 0) {
            std::fprintf(stderr, "warning: %llu games of %s could not be read and are left out of the book of %s\n",
                         static_cast<unsigned long long>(book->stats.skipped), file.c_str(), need.participant.c_str());
        }
        if (!book->stats.stopped.empty()) {
            std::fprintf(stderr, "warning: the reading of %s stopped before its end: %s\n", file.c_str(),
                         book->stats.stopped.c_str());
        }
    }
    return true;
}

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
        if (auto error = state_.pgnOutError(); error != out_error_) {
            if (!error.empty())
                std::fprintf(stderr, "warning: the games wait for %s: %s\n", spec.pgn_out.c_str(), error.c_str());
            out_error_ = std::move(error);
        }
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
    std::string out_error_;  // the last pgnOutError() reported
};

// Without a broadcast there is no observer of the games: this one measures the processor use of their engines, kept in
// processor.tsv for the window to show when it opens the tournament.
class CpuObserver final : public fastchess::observer::GameObserver, public tour::RecordListener {
   public:
    explicit CpuObserver(const tour::TournamentState& state)
        : meter_(state.spec().conditions.concurrency, state.dir() / "processor.tsv") {}

    void onGameStart(const fastchess::observer::GameStart& info) override { meter_.gameStart(info.slot); }
    void onEngineProcesses(int slot, int64_t white_pid, int64_t black_pid) override {
        meter_.engineProcesses(slot, white_pid, black_pid);
    }
    void onThinking(int slot, chess::Color, int64_t) override { meter_.thinking(slot); }
    void onMove(int slot, chess::Color mover, const fastchess::MoveData&, const std::string&, int64_t,
                int64_t) override {
        (void)meter_.moved(slot, mover);
    }
    void onGameEnd(int slot, std::size_t, std::size_t, const fastchess::MatchData&, bool counted) override {
        meter_.gameEnd(slot, counted);
    }
    void onRecorded(const tour::GameResult& result, const fastchess::MatchData&,
                    const std::optional<fastchess::Stats>&) override {
        meter_.recorded(result);
    }

   private:
    CpuMeter meter_;
};

// Each recorded game to two listeners, in turn.
class BothListeners final : public tour::RecordListener {
   public:
    BothListeners(tour::RecordListener& first, tour::RecordListener& second) : first_(first), second_(second) {}

    void onRecorded(const tour::GameResult& result, const fastchess::MatchData& data,
                    const std::optional<fastchess::Stats>& pair) override {
        first_.onRecorded(result, data, pair);
        second_.onRecorded(result, data, pair);
    }

   private:
    tour::RecordListener& first_;
    tour::RecordListener& second_;
};

}  // namespace

int runHeadless(const std::string& target, const Storage& storage, const CpuTopology& topology,
                const HeadlessBroadcast& broadcast) {
    std::unique_ptr<tour::TournamentState> state;
    BroadcastSettings settings;
    // Hash tables larger than the memory installed cannot work; larger than the free memory, they swap.
    const auto memoryFits = [&](const tour::TournamentSpec& spec, const std::vector<EngineEntry>& engines) {
        const auto memory = specMemory(spec, engines);
        if (memory.aboveInstalled() || memory.aboveAvailable()) {
            std::fprintf(stderr, "%s: %s\n", memory.aboveInstalled() ? "error" : "warning", memory.text().c_str());
        }
        return !memory.aboveInstalled();
    };
    try {
        if (tour::TournamentState::isTournament(target)) {
            state = tour::TournamentState::open(target);
            if (!state->finished() && !memoryFits(state->spec(), storage.loadEngines())) return 2;
            // Its PGN books, made again if their files changed since.
            if (!state->finished() && !preparePgnBooks(pgnBookNeeds(state->spec()))) return 2;
        } else {
            // A setup or template file: a new tournament, with the engines of the profile.
            const auto setup = readJson<MatchSetup>(target);
            if (!setup) {
                std::fprintf(stderr, "%s is neither a tournament folder nor a setup file\n", target.c_str());
                return 2;
            }
            settings.files      = setup->broadcast_files;
            settings.dir        = setup->broadcast_dir;
            settings.tlcs       = setup->broadcast_tlcs;
            settings.tlcs_port  = setup->tlcs_port;
            settings.tlcs_lanes = setup->tlcs_lanes;
            settings.web        = setup->broadcast_web;
            settings.web_port   = setup->web_port;
            settings.chat_link  = setup->web_chat_link;
            settings.discord_id = setup->web_discord_id;
            if (!setup->web_chat_link.empty() && webChatLink(setup->web_chat_link).empty()) {
                std::fprintf(stderr, "warning: web_chat_link is not an https link, left out\n");
            }
            if (!setup->web_discord_id.empty() && webDiscordId(setup->web_discord_id).empty()) {
                std::fprintf(stderr,
                             "warning: web_discord_id is not a Discord server ID (17 to 20 digits), left out\n");
            }
            settings.tunnel     = setup->broadcast_tunnel && (setup->tunnel_service == 0 || !setup->tunnel_host.empty());
            settings.tunnel_to  = tunnelSettings(*setup, storage.root() / "known_hosts.json");
            const auto engines  = storage.loadEngines();
            const auto dir =
                storage.runsDir() /
                (setup->format == TournamentFormat::Match
                     ? makeRunDirName(setup->engine_a, setup->engine_b)
                     : makeTournamentDirName(setup->name.empty() ? defaultTournamentName(*setup) : setup->name));
            if (!preparePgnBooks(pgnBookNeeds(*setup))) return 2;
            const auto run = buildRun(*setup, engines, topology, dir);
            if (!memoryFits(run.spec, engines)) return 2;
            state = tour::TournamentState::create(dir, run.spec);
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

    // The games that count, as the total does: voided games are not.
    const auto results = state->results();
    const auto played  = static_cast<uint64_t>(
        std::count_if(results.begin(), results.end(), [](const tour::GameResult& r) { return !r.voided; }));
    std::printf("%s: %s\nFolder: %s\nCtrl+C stops it; it resumes where it stopped with --run on the folder.\n\n",
                state->spec().name.c_str(),
                results.empty() ? "starting" : fmt::format("resuming, {} games played", played).c_str(),
                state->dir().string().c_str());

    ConsoleListener listener(*state);
    listener.setAlready(played);

    // The command line comes before the setup file.
    if (broadcast.tlcs_port) settings.tlcs = true, settings.tlcs_port = *broadcast.tlcs_port;
    if (broadcast.tlcs_lanes) settings.tlcs_lanes = true;
    if (broadcast.web_port) settings.web = true, settings.web_port = *broadcast.web_port;
    if (broadcast.live_dir) settings.files = true, settings.dir = *broadcast.live_dir;
    if (broadcast.lichess) {
        // The token from the environment, or the profile's.
        const char* token      = std::getenv("FCG_LICHESS_TOKEN");
        settings.lichess       = true;
        settings.lichess_token = token && *token ? token : storage.lichessToken();
        settings.lichess_round = *broadcast.lichess == "new" ? "" : *broadcast.lichess;
        if (settings.lichess_token.empty()) {
            std::fprintf(stderr, "error: --lichess needs a token in FCG_LICHESS_TOKEN (scope study:write)\n");
            return 2;
        }
    }
    if (broadcast.tunnel) {
        std::string error;
        auto to = parseTunnel(*broadcast.tunnel, error);
        if (!to) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 2;
        }
        if (to->pinned.empty()) to->known_hosts = storage.root() / "known_hosts.json";
        // The tunnel leads to the web page: served too, on its port.
        settings.web       = true;
        settings.tunnel    = true;
        settings.tunnel_to = *to;
    }
    const bool broadcasting = settings.files || settings.tlcs || settings.web || settings.lichess;

    // Broadcasting needs the games in progress: the observer the window uses, read by a thread of its own. It also
    // measures the processor use of the engines; without it, a lighter observer does only that.
    std::unique_ptr<GuiObserver> observer;
    std::unique_ptr<CpuObserver> cpu_observer;
    Broadcaster broadcaster;
    std::atomic_bool playing{true};
    std::thread broadcast_thread;
    if (broadcasting) {
        for (const auto& error : broadcaster.configure(settings)) std::fprintf(stderr, "warning: %s\n", error.c_str());
        if (broadcaster.tlcs().running()) {
            std::printf(settings.tlcs_lanes ? "TLCS server on UDP port %d and the next ones, one per game\n"
                                            : "TLCS server on UDP port %d\n",
                        settings.tlcs_port);
        }
        if (broadcaster.web().running()) std::printf("Web page on http://localhost:%d/\n", settings.web_port);
        if (settings.lichess) std::printf("Pushing the games to lichess\n");
        if (settings.tunnel && broadcaster.web().running()) {
            std::printf("Tunnel to %s for the web page on the internet\n", settings.tunnel_to.host.c_str());
        }
        if (settings.files) {
            std::printf("Live files in %s\n",
                        (settings.dir.empty() ? state->dir() / "live" : settings.dir).string().c_str());
        }
        std::printf("\n");

        observer         = std::make_unique<GuiObserver>(state->spec().conditions.concurrency, *state);
        broadcast_thread = std::thread([&] {
            std::vector<GameRecord> games;
            std::vector<fastchess::Stats> stats;
            // lichess: its link once on air, and every new error.
            std::string lichess_url, lichess_error;
            const auto lichessStatus = [&] {
                if (!settings.lichess) return;
                const auto s = broadcaster.lichess();
                if (!s.url.empty() && s.url != lichess_url) {
                    lichess_url = s.url;
                    std::printf("lichess: %s\n", s.url.c_str());
                }
                if (s.error != lichess_error) {
                    lichess_error = s.error;
                    if (!s.error.empty()) std::fprintf(stderr, "warning: %s\n", s.error.c_str());
                }
            };
            // The tunnel: its address each time it changes, and every new error.
            std::string tunnel_url, tunnel_error;
            const auto tunnelStatus = [&] {
                if (!settings.tunnel) return;
                const auto s = broadcaster.tunnel();
                // None while the connection is lost: told again once it is back, even the same.
                if (s.url.empty()) tunnel_url.clear();
                if (!s.url.empty() && s.url != tunnel_url) {
                    tunnel_url = s.url;
                    std::printf("Web page on the internet: %s\n", s.url.c_str());
                    std::fflush(stdout);
                }
                if (s.error != tunnel_error) {
                    tunnel_error = s.error;
                    if (!s.error.empty()) {
                        std::fprintf(stderr, "warning: tunnel: %s%s\n", s.error.c_str(),
                                     s.state == TunnelState::Failed ? "" : " (connecting again)");
                    }
                }
            };
            while (playing) {
                broadcaster.update(*state, *observer, std::nullopt, -1);
                lichessStatus();
                tunnelStatus();
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
            lichessStatus();
        });
    }

    fastchess::observer::GameObserver* games = observer.get();
    tour::RecordListener* recorder           = observer.get();
    if (!observer) {
        cpu_observer = std::make_unique<CpuObserver>(*state);
        games        = cpu_observer.get();
        recorder     = cpu_observer.get();
    }
    BothListeners listeners(*recorder, listener);

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    configureFastchess(state->spec());
    const auto error = playTournament(*state, games, &listeners);
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
