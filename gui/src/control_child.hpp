#pragma once

// The tournament process's side of the control channel (control.hpp): fastchess-gui --run <folder> --control stdio
// plays the tournament as the headless mode does, and talks with the window instead of printing.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <json.hpp>

#include <tour/budget.hpp>

#include "control.hpp"
#include "live.hpp"

namespace fcg {

// How the tournament process is driven: by nothing (it prints, as a terminal's --run), or through the control
// channel on its standard input and output (the window started it), or on a local socket (it outlives the window).
struct ControlOptions {
    enum class Mode { None, Stdio, Socket };
    Mode mode   = Mode::None;
    bool detach = false;  // --detach: leaves the session it was started in, and listens on its socket
    // What it may use of the machine until the window says otherwise (--allowance), so that it plays no more than that
    // from its first game.
    std::optional<tour::Allowance> allowance;
};

// --allowance THREADS:MEMORY_MB:GPUS:ONE (-1 for no limit, ONE 1 or 0), and back.
[[nodiscard]] std::optional<tour::Allowance> parseAllowance(const std::string& text);
[[nodiscard]] std::string allowanceArg(const tour::Allowance& allowance);

// The channel, the tournament's end: events out, numbered; commands in, taken by the control loop.
class ChildChannel {
   public:
    virtual ~ChildChannel() = default;

    // Sends {"seq": N, "e": kind, ...fields}. Thread safe. Dropped when no window listens.
    void send(const char* kind, nlohmann::json fields = nlohmann::json::object());
    // The hello, before anything else.
    void hello(const control::Hello& hello);

    // The commands that came since the last call.
    [[nodiscard]] std::vector<nlohmann::json> takeCommands();
    // The window went away (its end of the channel closed).
    [[nodiscard]] bool closed() const noexcept { return closed_; }

   protected:
    // Writes one line (with its end); false when it cannot.
    virtual bool write(const std::string& line) = 0;
    // A line the window sent.
    void received(const std::string& line);
    void setClosed() { closed_ = true; }

    std::mutex write_mutex_;  // held while a line is written: lines never mix

   private:
    uint64_t seq_ = 0;
    std::mutex commands_mutex_;
    std::vector<nlohmann::json> commands_;
    std::atomic_bool closed_{false};
};

// On the process's standard input and output, which the window's pipes are. Once made, nothing else can write to the
// standard output (it goes to `log`, as the standard error does): a stray line would break the channel.
class StdioChannel final : public ChildChannel {
   public:
    explicit StdioChannel(const std::string& log);
    ~StdioChannel() override;

   private:
    bool write(const std::string& line) override;
    void readLoop();

    int out_ = -1;  // the window's pipe, kept apart from the standard output
    int in_  = -1;
};

// The games, told to the window as they are played, with what measures the engines' processor use (kept in
// processor.tsv) and the window's observer, which the broadcast reads and which ends a game for the operator.
class ControlObserver final : public fastchess::observer::GameObserver, public tour::RecordListener {
   public:
    ControlObserver(ChildChannel& channel, const tour::TournamentState& state);

    [[nodiscard]] GuiObserver& live() noexcept { return live_; }

    // The searches held back (a few updates per second of each), sent once they are old enough. From the control loop.
    void flush();
    // The games in progress as they stand, for a window that connects to the tournament already playing.
    void sendSlots();

    void onGameStart(const fastchess::observer::GameStart& info) override;
    void onEngineProcesses(int slot, int64_t white_pid, int64_t black_pid) override;
    void onThinking(int slot, chess::Color stm, int64_t time_left_ms) override;
    void onEngineLine(int slot, chess::Color side, std::string_view line) override;
    void onMove(int slot, chess::Color mover, const fastchess::MoveData& move, const std::string& san,
                int64_t white_time_left, int64_t black_time_left) override;
    void onGameEnd(int slot, std::size_t game_id, std::size_t round_id, const fastchess::MatchData& data,
                   bool counted) override;
    void onRecorded(const tour::GameResult& result, const fastchess::MatchData& data,
                    const std::optional<fastchess::Stats>& pair) override;
    void onStartFailure(const tour::StartFailureEvent& event) override;

   private:
    // The last search line of each side of each slot not sent yet, and when one was last sent.
    struct Search {
        std::string line;
        Clock::time_point sent{};
    };
    void sendSearch(int slot, int side, std::unique_lock<std::mutex>& lock);

    ChildChannel& channel_;
    bool frc_ = false;
    CpuMeter meter_;
    GuiObserver live_;
    std::mutex search_mutex_;
    std::vector<std::array<Search, 2>> searches_;
};

}  // namespace fcg
