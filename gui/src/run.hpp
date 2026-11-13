#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <types/engine_config.hpp>
#include <types/tournament.hpp>

#include <tour/state.hpp>
#include <tour/summary.hpp>

#include "live.hpp"
#include "model.hpp"
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
    double elo0 = 0, elo1 = 0, alpha = 0, beta = 0;
    std::string model;
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

// An engine of the list as a new participant of `spec`: the tournament's time control (or nodes) and its common
// Hash and Threads, taken from the first participant; SyzygyPath and OwnBook if the setup forced them (as
// TournamentSpec::forced_options records), its own options otherwise.
[[nodiscard]] fastchess::EngineConfiguration participantConfig(const EngineEntry& entry,
                                                               const tour::TournamentSpec& spec);

// Sets fastchess' global configuration to play `spec` (one tournament at a time, fastchess keeps it in globals).
void configureFastchess(const tour::TournamentSpec& spec);

// Plays `tournament` on this thread until it is finished or fastchess::atomic::stop is set; configureFastchess()
// first. Returns an error message, empty on success.
[[nodiscard]] std::string playTournament(tour::TournamentState& tournament, fastchess::observer::GameObserver* observer,
                                         tour::RecordListener* listener);

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

// The participants of a setup in order, with their own settings: A and B of a match (with their books), the list
// otherwise.
[[nodiscard]] std::vector<ParticipantSetup> setupParticipants(const MatchSetup& setup);

// A participant's own book as the setup shows it: the library's name for it, else its file's name; empty when none.
[[nodiscard]] std::string ownBookName(const OwnBook& book);

// "own book A, 12 plies" ("own book A, all of it" for depth 0), for the summary and the tooltips.
[[nodiscard]] std::string ownBookText(const std::string& name, int depth);

// The names the participants of `setup` play under, in the PGN files, the standings and the ratings: the engine's,
// or, for one with a book of its own whose engine plays more than once, "<engine> (<book>)". Not necessarily
// distinct: buildRun() refuses a setup whose names are not.
[[nodiscard]] std::vector<std::string> participantNames(const MatchSetup& setup);

// Owns the thread a tournament runs on, and the tournament's state. Only one run exists at a time, because
// fastchess keeps its configuration and stop flag in globals.
class RunController {
   public:
    enum class State { Idle, Running, Finished };

    RunController() = default;
    ~RunController();

    // Starts a run; returns an error message, empty on success.
    std::string start(const MatchSetup& setup, const std::vector<EngineEntry>& engines, const CpuTopology& topology,
                      const std::filesystem::path& runs_root);

    // Continues the match stored in `dir`; returns an error message, empty on success.
    std::string resume(const std::filesystem::path& dir);

    // Opens the match stored in `dir` to look at it, without playing; returns an error message, empty on success.
    std::string view(const std::filesystem::path& dir);

    // Changes the open tournament, running or not; returns an error message, empty on success.
    std::string applyEdit(const tour::Edit& edit);
    // Undoes the last change while no game started since (TournamentState::canUndo()).
    std::string undoEdit();

    void setPaused(bool paused);
    [[nodiscard]] bool paused() const;

    // Interrupts the running games (they are not scored) and ends the run.
    void stopNow();
    // Pauses, waits for the running games to finish, then ends the run.
    void stopWhenIdle();
    [[nodiscard]] bool stoppingWhenIdle() const noexcept { return stop_when_idle_; }
    [[nodiscard]] bool stopping() const noexcept { return stop_requested_; }

    // Call once per frame.
    void update();

    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] bool running() const noexcept { return state_ == State::Running; }

    // Games that passed the pause gate and are not finished yet.
    [[nodiscard]] int runningGames() const;

    [[nodiscard]] GuiObserver* observer() const noexcept { return observer_.get(); }
    [[nodiscard]] const tour::TournamentState* tournament() const noexcept { return tournament_.get(); }
    [[nodiscard]] const RunInfo& info() const noexcept { return info_; }

    // Why the run thread ended: an error message (empty if none), and whether the user stopped it.
    [[nodiscard]] std::string error() const;
    [[nodiscard]] bool stoppedByUser() const noexcept { return stop_requested_; }

    // True exactly once, on the first update() after the run ended.
    [[nodiscard]] bool takeFinishedEvent();

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
