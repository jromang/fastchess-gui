#pragma once

// The window's end of a tournament process: started by the window (fastchess-gui --run <folder> --control stdio), or
// a process already running that the window connects to (its local socket, see local_socket.hpp). A thread reads
// what it sends; the GUI thread takes the messages once per frame.

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <json.hpp>

#include "child_process.hpp"
#include "control.hpp"

namespace fcg {

// Both ends of a connection: a pipe pair or a socket.
class Link {
   public:
    virtual ~Link() = default;
    // Waits for something: the bytes read, 0 at the end, -1 on an error.
    virtual long read(char* buffer, std::size_t size) = 0;
    virtual bool write(const std::string& data)       = 0;
    // Ends the connection: a read() waiting returns.
    virtual void close() = 0;
};

class TournamentProcess {
   public:
    TournamentProcess() = default;
    ~TournamentProcess();
    TournamentProcess(const TournamentProcess&)            = delete;
    TournamentProcess& operator=(const TournamentProcess&) = delete;

    // Starts `program` with `args` (--run <folder> --control stdio ...) and reads what it says. False with `error`.
    bool start(const std::string& program, const std::vector<std::string>& args, std::string& error,
               bool detached = false);
    // Talks with a process through `link` (its socket), which it owns from then on.
    void attach(std::unique_ptr<Link> link);

    // The messages that came since the last call, in their order: the hello, then the events.
    [[nodiscard]] std::vector<nlohmann::json> take();
    // Sends a line; false when the process cannot be told any more.
    bool send(const nlohmann::json& message);

    // The connection ended: the process went away (or closed it).
    [[nodiscard]] bool closed() const noexcept { return closed_; }
    // The process started here ended: its exit code; nullopt while it runs, or when it was not started here.
    [[nodiscard]] std::optional<int> exitCode();
    // Ends the connection without ending the process (one detached from the window goes on).
    void disconnect();
    // Ends the process at once (one started here).
    void kill();
    [[nodiscard]] int64_t pid() const noexcept { return child_ ? child_->pid() : 0; }

   private:
    void readLoop();

    std::unique_ptr<ChildProcess> child_;
    std::unique_ptr<Link> link_;
    std::thread reader_;
    std::mutex mutex_;
    std::vector<nlohmann::json> messages_;
    std::atomic_bool closed_{false};
};

}  // namespace fcg
