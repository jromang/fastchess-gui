#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <types/engine_config.hpp>
#include <types/tournament.hpp>

#include <tour/budget.hpp>
#include <tour/state.hpp>
#include <tour/summary.hpp>

#include "live.hpp"
#include "model.hpp"
#include "setup_checks.hpp"
#include "topology.hpp"

namespace fcg {

// What a run was started with, for display.
struct RunInfo {
    tour::Format format = tour::Format::Match;
    std::string name;  // the tournament's
    int participants = 2;
    std::string engine_a;  // match: the two engines; otherwise the first two participants
    std::string engine_b;
    std::filesystem::path dir;

    MatchMode mode  = MatchMode::Fixed;
    LimitKind limit = LimitKind::Nodes;
    std::string limit_text;
    uint64_t total_games = 0;  // 0 = until stopped
    int concurrency      = 1;
    bool affinity        = false;

    bool sprt   = false;
    double elo0 = 0, elo1 = 0, alpha = 0, beta = 0;  // the bounds the SPRT is given, the bias on them
    std::string model;
    double sprt_bias        = 0;  // tour::TournamentSpec::sprt_bias
    uint64_t sprt_min_games = 0;
    tour::Colours colours   = tour::Colours::Alternate;  // a match: who has White
    bool penta = true;

    Clock::time_point started{};
    Clock::time_point finished{};
};

struct BuiltRun {
    fastchess::config::Tournament tournament;
    std::vector<fastchess::EngineConfiguration> engines;  // [0] = A, [1] = B
    tour::TournamentSpec spec;                            // the match as the tournament layer stores it
    RunInfo info;
};

// What the toolbar and the views show about a match, from its stored settings.
[[nodiscard]] RunInfo runInfo(const tour::TournamentSpec& spec, const std::filesystem::path& dir);

// "40/60+0.6", "1 s/move", "1000 nodes", "depth 12".
[[nodiscard]] std::string limitText(const fastchess::Limit& limit);

// The settings of `spec`'s participants without settings of their own: those it recorded, or, in a tournament of 0.6.5
// or earlier, its first participant's (which may have had its own).
[[nodiscard]] tour::Defaults tournamentDefaults(const tour::TournamentSpec& spec);

// An engine of the list as a new participant of `spec`: the tournament's settings (tournamentDefaults()) unless `own`
// gives its own Hash, Threads, share of the time, nodes or depth (0, 100 %: the tournament's); SyzygyPath and OwnBook
// if the setup forced them (as TournamentSpec::forced_options records), its own options otherwise.
[[nodiscard]] fastchess::EngineConfiguration participantConfig(const EngineEntry& entry,
                                                               const tour::TournamentSpec& spec,
                                                               const ParticipantSetup& own = {});

// Sets fastchess' global configuration to play `spec` (one tournament at a time, fastchess keeps it in globals).
void configureFastchess(const tour::TournamentSpec& spec);

// Plays `tournament` on this thread until it is finished or fastchess::atomic::stop is set; configureFastchess()
// first. Returns an error message, empty on success. `on_runner`, when given, is told the runner once it exists (to
// set its allowance, read what it plays), and nullptr before it goes away.
[[nodiscard]] std::string playTournament(tour::TournamentState& tournament, fastchess::observer::GameObserver* observer,
                                         tour::RecordListener* listener,
                                         const std::function<void(tour::Runner*)>& on_runner = {});

// Ends the engine process `pid` of a game being played, with what it started (the engine behind a WinBoard adapter
// or a book adapter), as interruptEngineProcesses() ends them all.
void stopEngineProcess(int64_t pid);

// The most recent match in `runs_root`, if it is not finished (stopped, or interrupted by a crash).
[[nodiscard]] std::optional<tour::Summary> unfinishedMatch(const std::filesystem::path& runs_root);

// Turns the GUI setup into fastchess configuration, sanitized the way the command line would be.
// Throws fastchess_exception with a readable message on invalid input.
[[nodiscard]] BuiltRun buildRun(const MatchSetup& setup, const std::vector<EngineEntry>& engines,
                                const CpuTopology& topology, const std::filesystem::path& run_dir);

// A name made fit for a file name: anything but [A-Za-z0-9._-] becomes '_', at most 60 characters.
[[nodiscard]] std::string cleanName(std::string s);

// A name the user typed, as a file name on any system (see tour::fileNameOf()).
using tour::fileNameOf;

[[nodiscard]] std::string makeRunDirName(const std::string& a, const std::string& b);

// Chess960 starting position number n (0-959, Scharnagl's numbering: 518 is the usual one), as a FEN.
[[nodiscard]] std::string chess960Fen(int n);

// The Threads option of an engine's configuration, 1 when it has none.
[[nodiscard]] int configThreads(const fastchess::EngineConfiguration& config);

// The most pieces a position can have for the tablebase adjudication to end the game: its own limit, or the
// largest Syzygy tables found in its directories; 0 when it is off.
[[nodiscard]] int tablebasePieces(const fastchess::config::TbAdjudication& tb);

// A quick test of `engine`: a short match against `opponent` (the engine itself when empty, under another name) at
// 10+0.1, from a few positions written to `book`, every game played twice. `engines` receives the library to start
// it with (a copy of the engine for a match against itself).
[[nodiscard]] MatchSetup quickTestSetup(const EngineEntry& engine, const std::string& opponent,
                                        const std::filesystem::path& book, std::vector<EngineEntry>& engines);

// What a quick test showed about the engine of participant `id`: its result, its losses on time, crashes and
// illegal moves, and the share of its clock it used. `time_used_ms` and `moves` are its totals.
struct QuickTestReport {
    bool ok = false;
    std::string message;
};
[[nodiscard]] QuickTestReport quickTestReport(const tour::TournamentState& tournament, int id, uint64_t time_used_ms,
                                              uint64_t moves);

// How deep the lines of a Polyglot book are followed when the setup does not say (book_plies -1).
inline constexpr int POLYGLOT_PLIES = 8;

// Writes the 960 starting positions to `path` as an EPD book, unless it is there already.
void writeChess960Book(const std::filesystem::path& path);

// "20261109-185927_<name>" for a round robin or a gauntlet.
[[nodiscard]] std::string makeTournamentDirName(const std::string& name);

// The games a setup plans, 0 for a match played until stopped or decided by the SPRT (without a game limit). A
// knockout's tiebreaks are not counted.
[[nodiscard]] uint64_t plannedGames(const MatchSetup& setup);

// About how long a setup's games last: a game takes about two base times and 60 increments, or 120 moves, per
// side. nullopt without a clock (nodes, depth) or a game count.
[[nodiscard]] std::optional<double> plannedSeconds(const MatchSetup& setup, const CpuTopology& topology);

// The engine processes running now: the games', the engine reads' and checks'.
[[nodiscard]] int engineProcesses();

// Ends every engine process running now, at once: the reads they answer fail as if the engines had stopped. For a
// start that interrupts the reads and checks in flight, so only when no tournament, quick test or test suite runs.
// The processes are left to the threads that started them, which close and reap them.
void interruptEngineProcesses();

// The tournament's name when the user gave none: "A vs B", "Round robin, 8 engines"...
[[nodiscard]] std::string defaultTournamentName(const MatchSetup& setup);

// A team match's team (0: A, 1: B) as the window shows it: its name, "Team A" or "Team B" translated without one.
// The exports and the PGN tags write spec.teamName(), in English.
[[nodiscard]] std::string teamLabel(const tour::TournamentSpec& spec, int team);

// The participants of a setup in order, with their own settings: A and B of a match (with their books), the list
// otherwise.
[[nodiscard]] std::vector<ParticipantSetup> setupParticipants(const MatchSetup& setup);

// A participant's own book as the setup shows it: the library's name for it, else its file's name; empty when none.
[[nodiscard]] std::string ownBookName(const OwnBook& book);

// "own book A, 12 plies" ("own book A, all of it" for depth 0), for the summary and the tooltips.
[[nodiscard]] std::string ownBookText(const std::string& name, int depth);

// Why the Polyglot book `path` cannot be a participant's own, empty when it can: it cannot be read, is no Polyglot
// book, or its entries are not sorted and it is too large to be sorted in memory. Reads the file (all of it up to
// 16 MB, entries spread over it beyond).
[[nodiscard]] std::string ownBookProblem(const std::string& path);

// Why the PGN book `path` cannot be a participant's own for `depth`, empty when it can: turned into a Polyglot book
// now, unless the cache has it (tour/pgn_book.hpp).
[[nodiscard]] std::string pgnBookProblem(const std::string& path, int depth, bool chess960);

// The PGN books of the participants of a setup, or of a tournament, which are turned into Polyglot books before
// it starts. Those of a tournament's active participants only: one withdrawn or replaced plays no more games, and
// its book (deleted since, perhaps) does not hold the resume.
[[nodiscard]] std::vector<OwnBookNeed> pgnBookNeeds(const MatchSetup& setup);
[[nodiscard]] std::vector<OwnBookNeed> pgnBookNeeds(const tour::TournamentSpec& spec);

// The names the participants of `setup` play under, in the PGN files, the standings and the ratings: the engine's,
// or, for one with a book of its own whose engine plays more than once, "<engine> (<book>)". Not necessarily
// distinct: buildRun() refuses a setup whose names are not.
[[nodiscard]] std::vector<std::string> participantNames(const MatchSetup& setup);

// A tournament the window shows: played on a thread of this process (RunController: quick tests, and a tournament
// opened to look at it), or in a process of its own (RemoteRun, remote_run.hpp: every tournament started or resumed).
// The views read only this, so that they do not know where a tournament plays.
class Run {
   public:
    enum class State { Idle, Running, Finished };

    virtual ~Run() = default;

    // Changes the open tournament, running or not; returns an error message, empty on success.
    virtual std::string applyEdit(const tour::Edit& edit) = 0;
    // Undoes the last change while no game started since (TournamentState::canUndo()).
    virtual std::string undoEdit() = 0;

    // Ends game `number` (tour::GameTask::number()), played in slot `slot`, with the operator's `result` for `why`;
    // returns an error message, empty on success. fastchess cannot be told to end one game, so the process of the
    // engine on move is stopped: fastchess ends the game at once (a disconnection, its engine started again for its
    // next game, as "Restart an engine that crashes" does, which this needs), and the runner records it with the
    // result asked for (tour::TournamentState::requestEnd()).
    virtual std::string endGame(int slot, uint64_t number, const std::string& result, const std::string& why) = 0;

    virtual void setPaused(bool paused)       = 0;
    [[nodiscard]] virtual bool paused() const = 0;

    // The engine of `participant`, which cannot start, is tried again as soon as a game of it can be played, rather
    // than at its next try (tour::StartFailures). False when the run is not playing, or that engine is not failing.
    virtual bool tryEngineAgain(int participant) = 0;

    // Interrupts the running games (they are not scored) and ends the run.
    virtual void stopNow() = 0;
    // Pauses, waits for the running games to finish, then ends the run.
    virtual void stopWhenIdle()                         = 0;
    [[nodiscard]] virtual bool stoppingWhenIdle() const = 0;
    [[nodiscard]] virtual bool stopping() const         = 0;

    // Call once per frame.
    virtual void update() = 0;

    [[nodiscard]] virtual State state() const = 0;
    [[nodiscard]] bool running() const { return state() == State::Running; }

    // Games that passed the pause gate and are not finished yet.
    [[nodiscard]] virtual int runningGames() const = 0;

    [[nodiscard]] virtual LiveSource* observer() const                    = 0;
    [[nodiscard]] virtual const tour::TournamentState* tournament() const = 0;
    [[nodiscard]] virtual const RunInfo& info() const                     = 0;

    // Why it ended: an error message (empty if none), whether the user stopped it, whether fastchess stopped it (an
    // engine that crashed without "Restart an engine that crashes"), and the engine fastchess said could not start
    // when that stopped it (nullopt when unknown here: the window reads its log).
    [[nodiscard]] virtual std::string error() const  = 0;
    [[nodiscard]] virtual bool stoppedByUser() const = 0;
    [[nodiscard]] virtual bool abnormal() const      = 0;
    [[nodiscard]] virtual std::optional<StartupFailure> startupFailed() const { return std::nullopt; }

    // True exactly once, on the first update() after the run ended.
    [[nodiscard]] virtual bool takeFinishedEvent() = 0;
    // True once after its copy of the tournament was opened again (an edit undone in its process): the views start
    // over from it.
    [[nodiscard]] virtual bool takeReopened() { return false; }

    // What it may use of the machine (budget.hpp), what its games in flight take, and what holds its next game back.
    // Only a tournament in a process of its own is shared out by the window.
    virtual void setAllowance(const tour::Allowance& /*allowance*/) {}
    [[nodiscard]] virtual tour::Allowance allowance() const { return {}; }
    [[nodiscard]] virtual tour::GameCost inFlight() const { return {}; }
    [[nodiscard]] virtual tour::Wait waiting() const { return tour::Wait::None; }
    // Played in a process of its own.
    [[nodiscard]] virtual bool remote() const { return false; }
    // Shown by another window, which its process talks with: this one shows it no more, and leaves it be.
    [[nodiscard]] virtual bool elsewhere() const { return false; }
    // A tournament looked at, not playing: lets go of its folder, shown as it stands, before the process that resumes
    // it takes the folder's lock (#191).
    virtual void letGoOfFolder() {}
};

// Owns the thread a tournament runs on, and the tournament's state. Only one runs at a time in a process, because
// fastchess keeps its configuration and stop flag in globals: the window plays here only its quick tests.
class RunController final : public Run {
   public:
    RunController() = default;
    ~RunController() override;

    // Starts a run; returns an error message, empty on success.
    std::string start(const MatchSetup& setup, const std::vector<EngineEntry>& engines, const CpuTopology& topology,
                      const std::filesystem::path& runs_root);

    // Continues the match stored in `dir`; returns an error message, empty on success.
    std::string resume(const std::filesystem::path& dir);

    // Opens the match stored in `dir` to look at it, without playing; returns an error message, empty on success.
    std::string view(const std::filesystem::path& dir);
    void letGoOfFolder() override;

    std::string applyEdit(const tour::Edit& edit) override;
    std::string undoEdit() override;
    std::string endGame(int slot, uint64_t number, const std::string& result, const std::string& why) override;

    void setPaused(bool paused) override;
    [[nodiscard]] bool paused() const override;
    bool tryEngineAgain(int participant) override;

    void stopNow() override;
    void stopWhenIdle() override;
    [[nodiscard]] bool stoppingWhenIdle() const noexcept override { return stop_when_idle_; }
    [[nodiscard]] bool stopping() const noexcept override { return stop_requested_; }

    void update() override;

    [[nodiscard]] State state() const noexcept override { return state_; }
    [[nodiscard]] int runningGames() const override;

    [[nodiscard]] GuiObserver* observer() const noexcept override { return observer_.get(); }
    [[nodiscard]] const tour::TournamentState* tournament() const noexcept override { return tournament_.get(); }
    [[nodiscard]] const RunInfo& info() const noexcept override { return info_; }

    [[nodiscard]] std::string error() const override;
    [[nodiscard]] bool stoppedByUser() const noexcept override { return stop_requested_; }
    [[nodiscard]] bool abnormal() const override;
    [[nodiscard]] bool takeFinishedEvent() override;

   private:
    void afterEdit();  // after a change or its undo: participants and totals
    std::string launch(std::unique_ptr<tour::TournamentState> tournament);
    void threadMain();

    std::thread thread_;
    std::unique_ptr<tour::TournamentState> tournament_;
    std::unique_ptr<GuiObserver> observer_;
    RunInfo info_;

    State state_ = State::Idle;
    std::atomic_bool thread_done_{false};
    bool stop_requested_ = false;
    bool stop_when_idle_ = false;
    bool finished_event_ = false;

    mutable std::mutex outcome_mutex_;
    std::string error_;
};

// Number of engine reads and checks in flight; a run never starts while one is, since both read the
// global fastchess configuration (its timeouts), which a start replaces. See startGate.
inline std::atomic_int g_probes_in_flight{0};

}  // namespace fcg
