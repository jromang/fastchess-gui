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

#include "live.hpp"
#include "model.hpp"
#include "topology.hpp"

namespace fcg {

// What a run was started with, for display.
struct RunInfo {
    std::string engine_a;
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
    RunInfo info;
};

// Turns the GUI setup into fastchess configuration, sanitized the way the command line would be.
// Throws fastchess_exception with a readable message on invalid input.
[[nodiscard]] BuiltRun buildRun(const MatchSetup& setup, const std::vector<EngineEntry>& engines,
                                const CpuTopology& topology, const std::filesystem::path& run_dir);

[[nodiscard]] std::string makeRunDirName(const std::string& a, const std::string& b);

// Owns the thread a tournament runs on. Only one run exists at a time, because fastchess keeps its
// configuration and stop flag in globals.
class RunController {
   public:
    enum class State { Idle, Running, Finished };

    RunController() = default;
    ~RunController();

    // Starts a run; returns an error message, empty on success.
    std::string start(const MatchSetup& setup, const std::vector<EngineEntry>& engines, const CpuTopology& topology,
                      const std::filesystem::path& runs_root);

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
    [[nodiscard]] const RunInfo& info() const noexcept { return info_; }

    // Why the run thread ended: an error message (empty if none), and whether the user stopped it.
    [[nodiscard]] std::string error() const;
    [[nodiscard]] bool stoppedByUser() const noexcept { return stop_requested_; }

    // True exactly once, on the first update() after the run ended.
    [[nodiscard]] bool takeFinishedEvent();

   private:
    void threadMain(fastchess::config::Tournament tournament, std::vector<fastchess::EngineConfiguration> engines);

    std::thread thread_;
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

// Number of engine probes in flight; a run never starts while one is, since both read the
// global fastchess configuration.
inline std::atomic_int g_probes_in_flight{0};

}  // namespace fcg
