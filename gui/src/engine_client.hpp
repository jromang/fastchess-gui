#pragma once

// A small client for an engine of the library outside a tournament: the engine console of the Engines page, and the
// analysis and the games of the Board page. fastchess' engine class reads the tournament's global configuration
// (ping and startup times), and the end of a tournament ends every process on fastchess' list: this client drives
// the engine's process itself, with its own timeouts, and keeps it off that list, so that it can run while a
// tournament plays and outlive it.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "model.hpp"

namespace fcg {

// How an engine of the library is started: its program, arguments and working folder; a WinBoard engine through the
// adapter built into fastchess-gui (--wb2uci), as a tournament starts it, unless `raw` (the console speaks its own
// protocol to it).
struct EngineLaunch {
    std::string name;
    std::string dir;
    std::string program;
    std::string args;
};
[[nodiscard]] EngineLaunch engineLaunch(const EngineEntry& entry, bool raw = false);

// One engine process, its output read on a thread of its own. The lines it writes, and those sent to it, are kept in
// order until take() collects them.
class EngineProcess {
   public:
    struct Line {
        enum class Kind { Sent, Out, Err, Note };  // Note: what the client tells (started, ended)
        Kind kind = Kind::Out;
        std::string text;
        std::chrono::system_clock::time_point when;
    };

    EngineProcess();
    ~EngineProcess();  // stop()
    EngineProcess(const EngineProcess&)            = delete;
    EngineProcess& operator=(const EngineProcess&) = delete;

    // Starts the program. False, with the reason in `error`, when it cannot be started.
    bool start(const EngineLaunch& launch, std::string& error);

    // Writes `line` and an end of line. False when the engine is not running.
    bool send(const std::string& line);

    // The lines since the last call, oldest first.
    [[nodiscard]] std::vector<Line> take();

    // Waits until a line is there to take, the engine ended, or `timeout` passed. True when a line is there.
    bool wait(std::chrono::milliseconds timeout);

    // Started, and its output has not ended.
    [[nodiscard]] bool running() const;
    [[nodiscard]] bool started() const noexcept { return started_; }

    // Sends `quit` (when not empty) and gives the engine `grace` to end, then ends it with what it started. The
    // thread that reads it is joined. Nothing happens when it is not running.
    void stop(std::chrono::milliseconds grace = std::chrono::seconds(1), const std::string& quit = "quit");

   private:
    struct Impl;
    void push(Line::Kind kind, std::string text);
    void readLoop();

    std::unique_ptr<Impl> impl_;
    bool started_ = false;
    std::thread reader_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Line> pending_;
    bool ended_ = false;  // its output ended
};

// The engine threads the Board page uses now (its analysis and its games), for the core budget: a running tournament
// counts them against its cores, as the arbiter of several tournaments will (#131). A CoreClaim holds some of them for
// as long as it lives.
[[nodiscard]] int boardThreads();

class CoreClaim {
   public:
    CoreClaim() = default;
    explicit CoreClaim(int threads) { set(threads); }
    ~CoreClaim() { set(0); }
    CoreClaim(const CoreClaim&)            = delete;
    CoreClaim& operator=(const CoreClaim&) = delete;

    // Now `threads` instead of what it held.
    void set(int threads);
    [[nodiscard]] int threads() const noexcept { return threads_; }

   private:
    int threads_ = 0;
};

// "12:04:31.207", the local time of a line, for the console.
[[nodiscard]] std::string clockTime(std::chrono::system_clock::time_point when);

// The console's log as text, for a copy or a file: one line each, its time, then "> " before what was sent, "! "
// before the error output, "# " before what the console told.
[[nodiscard]] std::string consoleTranscript(const std::vector<EngineProcess::Line>& log);

// Adds a command typed in the console to its history (Up and Down bring them back): not twice in a row, at most 200.
void rememberCommand(std::vector<std::string>& history, const std::string& command);

}  // namespace fcg
