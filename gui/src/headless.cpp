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
#include <matchmaking/sprt/sprt.hpp>
#include <tour/pgn_book.hpp>
#include <tour/sprt_rules.hpp>
#include <tour/standings.hpp>
#include <tour/teams.hpp>

#include "broadcaster.hpp"
#include "console.hpp"
#include "control_child.hpp"
#include "engine_logos.hpp"
#include "memory.hpp"
#include "portable.hpp"
#include "run.hpp"
#include "setup_checks.hpp"

#ifdef _WIN32
#    include <process.h>
#else
#    include <unistd.h>
#endif

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
        std::printf("%s  %s - %s  %s  {%s}%s\n",
                    total ? fmt::format("[{}/{}]", played, *total).c_str() : fmt::format("[{}]", played).c_str(),
                    name(result.white).c_str(), name(result.black).c_str(), result.result.c_str(),
                    result.reason.c_str(), result.unbalanced != 0 ? "  unbalanced opening" : "");
        // Its pair voided, to be played again from another opening (TournamentSpec::unbalanced_replace).
        if (const auto replaced = state_.openingsReplaced(); replaced != replaced_) {
            replaced_ = replaced;
            std::printf("Unbalanced opening: game pair %u is played again from another opening.\n",
                        result.encounter + 1);
        }
        if (played_ % 20 == 0) printStatus();
        std::fflush(stdout);
        if (auto error = state_.pgnOutError(); error != out_error_) {
            if (!error.empty())
                std::fprintf(stderr, "warning: the games wait for %s: %s\n", spec.pgn_out.c_str(), error.c_str());
            out_error_ = std::move(error);
        }
    }

    // The engines that cannot start: each try, the engine set aside or back, and the stop for want of it.
    void onStartFailure(const tour::StartFailureEvent& event) override {
        if (event.engines.empty()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        using Kind     = tour::StartFailureEvent::Kind;
        const auto& e  = event.engines.front();
        const auto log = e.log.empty() ? std::string() : fmt::format("\n  engine log: {}", e.log.string());
        switch (event.kind) {
            case Kind::Retry:
                std::fprintf(stderr, "warning: %s did not start (try %d of %d): %s; tried again in %lld s\n",
                             e.name.c_str(), e.tries, tour::StartFailures::TRIES, e.error.c_str(),
                             static_cast<long long>(tour::StartFailures::RETRY_PAUSE.count()));
                break;
            case Kind::SetAside:
                std::fprintf(stderr,
                             "warning: %s cannot start: %s\n  Its games are set aside and the others go on; it is "
                             "tried again every %lld minutes.%s\n",
                             e.name.c_str(), e.error.c_str(),
                             static_cast<long long>(tour::StartFailures::RETRY_EVERY.count()), log.c_str());
                break;
            case Kind::StillFailing:
                std::fprintf(stderr, "warning: %s still cannot start: %s\n", e.name.c_str(), e.error.c_str());
                break;
            case Kind::Stopped:
                std::fprintf(stderr, "%s cannot start after %d tries: the tournament stops.%s\n", e.name.c_str(),
                             e.tries, log.c_str());
                break;
            case Kind::Started:
                std::printf("%s started again: its games are played again.\n", e.name.c_str());
                break;
            case Kind::Stalled: {
                std::string names;
                for (const auto& f : event.engines) names += (names.empty() ? "" : ", ") + f.name;
                std::fprintf(stderr,
                             "No game is left to play without %s, which cannot start: the tournament stops. Fix it, "
                             "or withdraw or replace it in the window, then resume.\n",
                             names.c_str());
                break;
            }
        }
        std::fflush(stdout);
        std::fflush(stderr);
    }

    void setAlready(uint64_t n) {
        already_  = n;
        replaced_ = state_.openingsReplaced();
    }

    void printStatus() const {
        const auto& spec = state_.spec();
        if (spec.format == tour::Format::Match) {
            const auto stats = state_.pairStats(spec.participants[0].id, spec.participants[1].id);
            if (stats.sum() == 0) return;
            const fastchess::elo::EloPentanomial elo(stats);
            // The LLR all along, its decision held until the minimum of games (sprt_rules.hpp).
            std::string llr;
            if (const auto& s = spec.conditions.sprt; s.enabled) {
                const fastchess::SPRT sprt(s.alpha, s.beta, s.elo0, s.elo1, s.model, true);
                llr = fmt::format(", LLR {:.2f} ({:.2f}, {:.2f})", sprt.getLLR(stats, true), sprt.getLowerBound(),
                                  sprt.getUpperBound());
                if (static_cast<uint64_t>(stats.sum()) < spec.sprt_min_games) {
                    llr += fmt::format(", no decision before {} games", spec.sprt_min_games);
                }
            }
            std::printf("--- %s vs %s: +%d =%d -%d, Elo %s%s\n", spec.participants[0].engine.name.c_str(),
                        spec.participants[1].engine.name.c_str(), stats.wins, stats.draws, stats.losses,
                        elo.getElo().c_str(), llr.c_str());
            return;
        }
        const auto results = state_.results();
        const auto table   = tour::computeTable(spec, results, std::nullopt, state_.byes());
        // A team match: the teams' score and their engines against each other.
        if (spec.format == tour::Format::TeamMatch) {
            std::printf("\n%s\n", tour::teamText(tour::computeTeams(spec, results), table, spec.name).c_str());
            return;
        }
        // Under the table, the line of the copies: the games, the draws, the time control, the book and how its
        // openings were chosen.
        std::printf("\n%s\n",
                    tour::tableText(table, spec.name, tour::footerLine(spec, table, state_.totalGames())).c_str());
    }

   private:
    const tour::TournamentState& state_;
    std::mutex mutex_;
    uint64_t played_   = 0;
    uint64_t already_  = 0;
    uint64_t replaced_ = 0;  // openings replaced as unbalanced, as last told
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

// What a tournament alone may use (from a terminal, or started without one): the memory and the GPUs of the
// profile's budget (Settings); its concurrency decides the rest.
tour::Allowance profileAllowance(const Storage& storage) {
    const auto budget = storage.machineBudget();
    tour::Allowance a;
    if (budget.memory_mb > 0) {
        a.memory_mb = budget.memory_mb;
    } else if (const auto memory = systemMemory()) {
        a.memory_mb = static_cast<int64_t>(memory->total_mb * MachineBudget::MEMORY_SHARE / 100);
    }
    if (budget.gpus > 0) a.gpus = budget.gpus;
    return a;
}

// Each recorded game to two listeners, in turn.
class BothListeners final : public tour::RecordListener {
   public:
    BothListeners(tour::RecordListener& first, tour::RecordListener& second) : first_(first), second_(second) {}

    void onRecorded(const tour::GameResult& result, const fastchess::MatchData& data,
                    const std::optional<fastchess::Stats>& pair) override {
        first_.onRecorded(result, data, pair);
        second_.onRecorded(result, data, pair);
    }
    void onStartFailure(const tour::StartFailureEvent& event) override {
        first_.onStartFailure(event);
        second_.onStartFailure(event);
    }

   private:
    tour::RecordListener& first_;
    tour::RecordListener& second_;
};

// Hash tables larger than the memory installed cannot work; larger than the free memory, they swap.
bool memoryFits(const tour::TournamentSpec& spec, const std::vector<EngineEntry>& engines) {
    const auto memory = specMemory(spec, engines);
    if (memory.aboveInstalled() || memory.aboveAvailable()) {
        std::fprintf(stderr, "%s: %s\n", memory.aboveInstalled() ? "error" : "warning", memory.text().c_str());
    }
    return !memory.aboveInstalled();
}

// A new tournament in the profile's tournaments folder, from a setup, with the profile's engines: nullptr when its PGN
// books cannot be prepared or its hash tables cannot fit (said on the standard error).
std::unique_ptr<tour::TournamentState> createTournament(const MatchSetup& setup, const Storage& storage,
                                                        const CpuTopology& topology) {
    const auto engines = storage.loadEngines();
    const auto dir =
        storage.runsDir() / (setup.format == TournamentFormat::Match
                                 ? makeRunDirName(setup.engine_a, setup.engine_b)
                                 : makeTournamentDirName(setup.name.empty() ? defaultTournamentName(setup) : setup.name));
    if (!preparePgnBooks(pgnBookNeeds(setup))) return nullptr;
    const auto run = buildRun(setup, engines, topology, dir);
    if (!memoryFits(run.spec, engines)) return nullptr;
    return tour::TournamentState::create(dir, run.spec);
}

}  // namespace

int runControlled(const std::string& target, const Storage& storage, const HeadlessBroadcast& broadcast,
                  const ControlOptions& control);

std::optional<std::filesystem::path> tournamentFromSetup(const std::string& file, const Storage& storage,
                                                         const CpuTopology& topology) {
    try {
        auto setup = readJson<MatchSetup>(file);
        if (!setup) {
            std::fprintf(stderr, "%s is neither a tournament folder nor a setup file\n", file.c_str());
            return std::nullopt;
        }
        // A template of a portable profile names the files of the program's folder relative to it.
        portable::resolve(*setup);
        const auto state = createTournament(*setup, storage, topology);
        if (!state) return std::nullopt;
        return state->dir();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return std::nullopt;
    }
}

int runHeadless(const std::string& target, const Storage& storage, const CpuTopology& topology,
                const HeadlessBroadcast& broadcast, const ControlOptions& control) {
    if (control.mode != ControlOptions::Mode::None) return runControlled(target, storage, broadcast, control);

    // The version first: what a log of the run, sent with a bug report, must tell.
    std::printf("fastchess-gui %s\n", FCG_VERSION);
    std::fflush(stdout);
    std::unique_ptr<tour::TournamentState> state;
    BroadcastSettings settings;
    try {
        if (tour::TournamentState::isTournament(target)) {
            state = tour::TournamentState::open(target);
            if (!state->finished() && !memoryFits(state->spec(), storage.loadEngines())) return 2;
            // Its PGN books, made again if their files changed since.
            if (!state->finished() && !preparePgnBooks(pgnBookNeeds(state->spec()))) return 2;
        } else {
            // A setup or template file: a new tournament, with the engines of the profile.
            auto setup = readJson<MatchSetup>(target);
            if (!setup) {
                std::fprintf(stderr, "%s is neither a tournament folder nor a setup file\n", target.c_str());
                return 2;
            }
            // A template of a portable profile names the files of the program's folder relative to it.
            portable::resolve(*setup);
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
            settings.tunnel    = setup->broadcast_tunnel && (setup->tunnel_service == 0 || !setup->tunnel_host.empty());
            settings.tunnel_to = tunnelSettings(*setup, storage.root() / "known_hosts.json");
            state = createTournament(*setup, storage, topology);
            if (!state) return 2;
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
    std::printf("%s: %s\nFolder: %s\n", state->spec().name.c_str(),
                results.empty() ? "starting" : fmt::format("resuming, {} games played", played).c_str(),
                state->dir().string().c_str());
    // A match's SPRT: the tester's bounds, the bias on them, the games before which it decides nothing.
    if (const auto& spec = state->spec(); spec.format == tour::Format::Match && spec.conditions.sprt.enabled) {
        const auto& s = spec.conditions.sprt;
        std::printf(
            "%s (%s, alpha %g, beta %g)\n",
            tour::sprtText(s.elo0 - spec.sprt_bias, s.elo1 - spec.sprt_bias, spec.sprt_bias, spec.sprt_min_games)
                .c_str(),
            s.model.c_str(), s.alpha, s.beta);
    }
    std::printf("Ctrl+C stops it; it resumes where it stopped with --run on the folder.\n\n");

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
        if (broadcaster.web().running()) {
            std::printf("Web page on http://localhost:%d/\n", settings.web_port);
            broadcaster.setLogos(logos::webLogos(storage.loadEngines(), storage.logosDir()));
        }
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
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(broadcast.end_wait_ms);
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
    const auto allowance = profileAllowance(storage);
    const auto error     = playTournament(*state, games, &listeners, [&](tour::Runner* runner) {
        if (runner) runner->setAllowance(allowance);
    });
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

namespace fcg {

namespace {

// The broadcast settings of the command line (the window passes those the tournament was started with).
BroadcastSettings commandLineBroadcast(const HeadlessBroadcast& broadcast, const Storage& storage) {
    BroadcastSettings settings;
    if (broadcast.tlcs_port) settings.tlcs = true, settings.tlcs_port = *broadcast.tlcs_port;
    if (broadcast.tlcs_lanes) settings.tlcs_lanes = true;
    if (broadcast.web_port) settings.web = true, settings.web_port = *broadcast.web_port;
    if (broadcast.live_dir) settings.files = true, settings.dir = *broadcast.live_dir;
    if (broadcast.lichess) {
        const char* token      = std::getenv("FCG_LICHESS_TOKEN");
        settings.lichess       = true;
        settings.lichess_token = token && *token ? token : storage.lichessToken();
        settings.lichess_round = *broadcast.lichess == "new" ? "" : *broadcast.lichess;
    }
    if (broadcast.tunnel) {
        std::string error;
        if (auto to = parseTunnel(*broadcast.tunnel, error)) {
            if (to->pinned.empty()) to->known_hosts = storage.root() / "known_hosts.json";
            settings.web       = true;
            settings.tunnel    = true;
            settings.tunnel_to = *to;
        }
    }
    return settings;
}

int64_t ownPid() {
#ifdef _WIN32
    return static_cast<int64_t>(_getpid());
#else
    return static_cast<int64_t>(::getpid());
#endif
}

}  // namespace

int runControlled(const std::string& target, const Storage& storage, const HeadlessBroadcast& broadcast,
                  const ControlOptions& control) {
    namespace fs = std::filesystem;
    const fs::path dir(target);
    // The channel first: from now on nothing else may write to the standard output, which goes to the log.
    std::error_code ec;
    const auto log_file = fs::is_directory(dir, ec) ? dir / "fastchess-gui.log" : storage.root() / "fastchess-gui.log";
    control::Hello hello;
    hello.version = FCG_VERSION;
    hello.dir     = fs::absolute(dir).lexically_normal().string();
    hello.pid     = ownPid();
    std::unique_ptr<ChildChannel> channel;
    if (control.mode == ControlOptions::Mode::Socket) {
        // On its socket, to play on without the window: a window connects when it wants, and again.
        auto socket = std::make_unique<SocketChannel>(log_file.string(), dir, hello);
        std::string error;
        if (!socket->listen(error)) {
            Console::instance().logTo(log_file, true);
            Console::instance().add(error, true);
            return 2;
        }
        channel = std::move(socket);
    } else {
        channel = std::make_unique<StdioChannel>(log_file.string());
        channel->hello(hello);
    }

    // What it says goes to the window and to the tournament's log.
    auto& console = Console::instance();
    console.logTo(log_file, true);
    console.setListener([&channel](const Console::Line& line) {
        channel->send("log", {{"text", line.text}, {"warning", line.warning}});
    });
    console.add(fmt::format("fastchess-gui {}: {} plays here (process {})", FCG_VERSION, hello.dir, hello.pid));
    const auto console_mark = console.added();

    control::End end;
    const auto finish = [&](int code) {
        end.exit_code = code;
        channel->send("end", control::endJson(end));
        console.setListener({});
        return code;
    };

    std::unique_ptr<tour::TournamentState> state;
    try {
        if (!tour::TournamentState::isTournament(dir)) throw std::runtime_error("no tournament in " + target);
        state = tour::TournamentState::open(dir);
    } catch (const std::exception& e) {
        end.error = e.what();
        console.add(end.error, true);
        return finish(2);
    }
    for (const auto& w : state->warnings()) console.add(w, true);
    if (state->finished()) {
        end.finished = true;
        return finish(0);
    }
    // Its PGN books, turned into Polyglot books when their files changed since (the window prepared them already).
    for (const auto& need : pgnBookNeeds(state->spec())) {
        if (const auto problem = pgnBookProblem(need.path, need.depth, need.chess960); !problem.empty()) {
            end.error = fmt::format("the book of {} cannot be used: {}", need.participant, problem);
            console.add(end.error, true);
            return finish(2);
        }
    }

    ControlObserver observer(*channel, *state);
    state->setJournalListener([&channel](uint64_t at, uint64_t last, const nlohmann::json& event) {
        channel->send("journal", {{"at", at}, {"end", last}, {"event", event}});
    });

    // Broadcasting from here: it goes on without the window.
    const auto settings     = commandLineBroadcast(broadcast, storage);
    const bool broadcasting = settings.files || settings.tlcs || settings.web || settings.lichess;
    Broadcaster broadcaster;
    if (broadcasting) {
        for (const auto& error : broadcaster.configure(settings)) console.add(error, true);
    }

    std::mutex runner_mutex;
    tour::Runner* runner = nullptr;
    auto allowance       = control.allowance.value_or(profileAllowance(storage));
    std::atomic_bool playing{true};
    bool stopped_by_user = false, stop_idle = false;

    const auto reply = [&](const nlohmann::json& command, const std::string& error) {
        if (command.contains("id")) channel->send("reply", {{"id", command["id"]}, {"error", error}});
    };
    const auto stopNow = [&] {
        fastchess::atomic::stop = true;
        fastchess::observer::PauseGate::instance().wakeAll();
    };
    const auto handle = [&](const nlohmann::json& c) {
        const auto kind = c.value("c", std::string{});
        auto& gate      = fastchess::observer::PauseGate::instance();
        try {
            if (kind == "pause" || kind == "resume") {
                gate.setPaused(kind == "pause");
            } else if (kind == "stop") {
                stopped_by_user = true;
                if (c.value("now", false)) {
                    stopNow();
                } else {
                    stop_idle = true;
                    gate.stopWhenIdle();
                }
            } else if (kind == "edit") {
                state->applyEdit(c.at("edit").get<tour::Edit>());
                observer.live().updateParticipants(state->spec());
                reply(c, {});
            } else if (kind == "undo") {
                state->undoEdit();
                observer.live().updateParticipants(state->spec());
                reply(c, {});
            } else if (kind == "end_game") {
                const auto number = c.at("number").get<uint64_t>();
                if (!state->spec().conditions.recover) throw std::runtime_error("no restart of crashed engines");
                if (!state->requestEnd(number, c.at("result").get<std::string>(), c.value("why", std::string{})))
                    throw std::runtime_error("this game is over already");
                if (!observer.live().stopEngineOnMove(c.at("slot").get<int>(), static_cast<std::size_t>(number + 1),
                                                      stopEngineProcess) &&
                    state->takeEnd(number)) {
                    throw std::runtime_error("this game is not being played");
                }
                reply(c, {});
            } else if (kind == "try_engine") {
                (void)state->startFailures().tryNow(c.at("participant").get<int>(), tour::StartFailures::Clock::now());
            } else if (kind == "allowance") {
                std::lock_guard<std::mutex> lock(runner_mutex);
                allowance = control::allowanceFromJson(c.at("allowance"));
                if (runner) runner->setAllowance(allowance);
            } else if (kind == "snapshot") {
                observer.sendSlots();
            }
        } catch (const std::exception& e) {
            reply(c, e.what());
        }
    };

    // The control loop: the window's commands, the searches held back, the state when it changes.
    std::thread loop([&] {
        control::Status last;
        bool sent       = false;
        auto next_state = Clock::now();
        std::vector<GameRecord> games;
        std::vector<fastchess::Stats> stats;
        while (playing) {
            // A window connected: the games in progress and the state, as they stand.
            if (channel->takeConnected()) {
                observer.sendSlots();
                sent       = false;
                next_state = Clock::now();
            }
            for (const auto& c : channel->takeCommands()) handle(c);
            // The window went away without a word (closed, crashed): the tournament stops, resumable.
            if (channel->closed() && control.mode == ControlOptions::Mode::Stdio && !fastchess::atomic::stop) {
                console.add("The window went away: the tournament stops, to be resumed.", true);
                stopNow();
            }
            auto& gate = fastchess::observer::PauseGate::instance();
            // Idle means no game running and no pair waiting for its second game, so no result is thrown away.
            if (stop_idle && !fastchess::atomic::stop && gate.running() == 0 && gate.openPairs() == 0) stopNow();
            observer.flush();
            observer.live().drain(games, stats);  // the window's lists, not kept here
            games.clear();
            stats.clear();
            if (broadcasting) broadcaster.update(*state, observer.live(), std::nullopt, -1);

            if (Clock::now() >= next_state) {
                next_state = Clock::now() + std::chrono::milliseconds(200);
                control::Status st;
                st.phase         = control::Status::Phase::Running;
                st.paused        = gate.paused();
                st.stop_idle     = stop_idle;
                st.stopping      = fastchess::atomic::stop;
                st.running_games = gate.running();
                st.open_pairs    = static_cast<int>(gate.openPairs());
                for (int i = 0; i < observer.live().slotCount(); ++i) {
                    SlotState slot;
                    if (observer.live().copySlot(i, slot) && slot.active && slot.game_id > 0)
                        st.running.push_back(slot.game_id - 1);
                }
                std::sort(st.running.begin(), st.running.end());
                st.can_undo       = state->canUndo();
                st.undo_lasts     = state->undoLasts();
                st.pgn_out_error  = state->pgnOutError();
                st.start_failures = state->startFailures().engines();
                st.stalled        = state->startFailures().stalled();
                {
                    std::lock_guard<std::mutex> lock(runner_mutex);
                    st.allowance = allowance;
                    if (runner) {
                        st.in_flight = runner->inFlight();
                        st.waiting   = runner->waiting();
                    }
                }
                if (!sent || !(st == last)) {
                    channel->send("state", control::statusJson(st));
                    last = st;
                    sent = true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });

    // Stopped by the system (shut down, a terminal's Ctrl+C on a tournament started from it): resumable.
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    configureFastchess(state->spec());
    const auto error    = playTournament(*state, &observer, &observer, [&](tour::Runner* r) {
        std::lock_guard<std::mutex> lock(runner_mutex);
        runner = r;
        if (runner) runner->setAllowance(allowance);
    });
    const bool abnormal = fastchess::atomic::abnormal_termination;
    playing             = false;
    loop.join();
    if (broadcasting) {
        // The end of the last games, then time for the viewers to see it.
        const auto until = Clock::now() + std::chrono::seconds(2);
        while (Clock::now() < until) {
            broadcaster.update(*state, observer.live(), std::nullopt, -1);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        broadcaster.stop();
    }
    state->setJournalListener({});

    control::Status final_state;
    final_state.phase          = control::Status::Phase::Finished;
    final_state.can_undo       = state->canUndo();
    final_state.start_failures = state->startFailures().engines();
    final_state.stalled        = state->startFailures().stalled();
    channel->send("state", control::statusJson(final_state));

    end.error           = error;
    end.abnormal        = abnormal;
    end.stopped_by_user = stopped_by_user;
    end.finished        = state->finished();
    end.stalled         = state->startFailures().stalled();
    if (error.empty() && abnormal && !stopped_by_user) {
        if (const auto failed = startupFailure(console.since(console_mark))) {
            end.failed_engine = failed->engine;
            end.failed_reason = failed->reason;
        }
    }
    return finish(!error.empty() ? 1 : state->finished() ? 0 : 3);
}

}  // namespace fcg
