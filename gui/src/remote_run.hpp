#pragma once

// A tournament played in a process of its own (fastchess-gui --run <folder> --control ...), as the window shows it: a
// read-only copy of the tournament kept current from the journal lines the process sends (TournamentState::follow()),
// and an observer of its games fed by its events (GuiObserver without measuring). The commands go to the process.

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "control.hpp"
#include "run.hpp"
#include "tournament_process.hpp"

namespace fcg {

// How to start a tournament process.
struct RemoteStart {
    std::string program;             // this program
    std::filesystem::path profile;   // the window's profile: the engines, the book cache
    std::filesystem::path dir;       // the tournament's folder (created already)
    std::vector<std::string> extra;  // more arguments (the broadcast: --tlcs PORT, --web PORT...)
    std::optional<tour::Allowance> allowance;
    // Out of this program's session, on its socket (--control socket): it plays on when the window closes, which
    // connects to it again. Otherwise on pipes (--control stdio), ending with the window.
    bool detached = false;
};

// The arguments of a tournament process.
[[nodiscard]] std::vector<std::string> remoteArgs(const RemoteStart& start);

class RemoteRun final : public Run {
   public:
    RemoteRun() = default;
    ~RemoteRun() override;

    // Starts the process playing the tournament in `start.dir` and opens its copy; an error message, empty on
    // success.
    std::string start(const RemoteStart& start);
    // Talks with a process playing the tournament in `dir` already, through `link`; an error message, empty on
    // success.
    std::string attach(const std::filesystem::path& dir, std::unique_ptr<Link> link);

    std::string applyEdit(const tour::Edit& edit) override;
    std::string undoEdit() override;
    std::string endGame(int slot, uint64_t number, const std::string& result, const std::string& why) override;

    void setPaused(bool paused) override;
    [[nodiscard]] bool paused() const override { return status_.paused; }
    bool tryEngineAgain(int participant) override;

    void stopNow() override;
    void stopWhenIdle() override;
    [[nodiscard]] bool stoppingWhenIdle() const override { return stop_when_idle_ || status_.stop_idle; }
    [[nodiscard]] bool stopping() const override { return stop_requested_; }

    void update() override;

    [[nodiscard]] State state() const override { return state_; }
    [[nodiscard]] int runningGames() const override { return state_ == State::Running ? status_.running_games : 0; }

    [[nodiscard]] LiveSource* observer() const override { return observer_.get(); }
    [[nodiscard]] const tour::TournamentState* tournament() const override {
        return writer_ ? writer_.get() : replica_.get();
    }
    [[nodiscard]] const RunInfo& info() const override { return info_; }

    [[nodiscard]] std::string error() const override { return error_; }
    [[nodiscard]] bool stoppedByUser() const override { return stop_requested_ || end_.stopped_by_user; }
    [[nodiscard]] bool abnormal() const override { return end_.abnormal; }
    [[nodiscard]] std::optional<StartupFailure> startupFailed() const override;
    [[nodiscard]] bool takeFinishedEvent() override;
    [[nodiscard]] bool takeReopened() override;

    void setAllowance(const tour::Allowance& allowance) override;
    [[nodiscard]] tour::Allowance allowance() const override { return allowance_; }
    [[nodiscard]] tour::GameCost inFlight() const override { return status_.in_flight; }
    [[nodiscard]] tour::Wait waiting() const override { return status_.waiting; }
    [[nodiscard]] bool remote() const override { return true; }
    [[nodiscard]] bool elsewhere() const override { return elsewhere_; }

    // What the process told last of itself, and how it ended.
    [[nodiscard]] const control::Status& status() const noexcept { return status_; }
    [[nodiscard]] const control::End& end() const noexcept { return end_; }
    [[nodiscard]] int64_t pid() const noexcept { return pid_; }
    // Leaves the process running without the window (it plays on, detached): the connection ends.
    void disconnect();
    // Still waiting to connect to the process it started (detached), which is making its socket.
    [[nodiscard]] bool connecting() const noexcept { return connecting_; }

   private:
    std::string open(const std::filesystem::path& dir);  // the copy and the observer
    void apply(const nlohmann::json& message);
    void reopen();  // the copy opened again, from the files
    // Sends a command and waits for its reply (the events before it applied); its error, empty when it worked.
    std::string ask(const char* kind, nlohmann::json fields);

    TournamentProcess process_;
    std::filesystem::path dir_;
    std::unique_ptr<tour::TournamentState> replica_;
    // Once its process ended, the tournament opened for writing here, for a change (a cycle more, games voided); kept
    // open, so that the change can be undone.
    std::unique_ptr<tour::TournamentState> writer_;
    // A change made once the process ended; an error message, empty on success.
    std::string localEdit(const std::function<void(tour::TournamentState&)>& change);
    std::unique_ptr<GuiObserver> observer_;
    RunInfo info_;
    control::Status status_;
    control::End end_;
    tour::Allowance allowance_;
    bool allowance_sent_ = false;

    // Detached: the socket it connects to, and until when it tries, the process making it, or connecting again after
    // the connection was lost.
    bool detached_   = false;
    bool connecting_ = false;
    std::chrono::steady_clock::time_point connect_until_{};
    void tryConnect();
    bool elsewhere_ = false;  // another window shows it: this one let go

    State state_         = State::Idle;
    bool stop_requested_ = false;
    bool stop_when_idle_ = false;
    bool finished_event_ = false;
    bool reopened_       = false;
    bool ended_          = false;  // its end event came
    std::string error_;
    int64_t pid_      = 0;
    uint64_t last_id_ = 0;
    std::map<uint64_t, std::string> replies_;
};

}  // namespace fcg
