#pragma once

// A program this one starts and talks with through pipes: a tournament process (fastchess-gui --run ... --control
// stdio), whose standard input and output are the control channel.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fcg {

class ChildProcess {
   public:
    ChildProcess() = default;
    ~ChildProcess();
    ChildProcess(const ChildProcess&)            = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // Starts `program` with `args`, its standard input and output our pipes, its standard error our own (none on
    // Windows: no console window either). With `detached`, it leaves this program's session (process group, console):
    // it goes on when this program ends, and Ctrl+C in this program's terminal does not reach it. False with `error`
    // when it cannot start.
    bool start(const std::string& program, const std::vector<std::string>& args, std::string& error,
               bool detached = false);

    // Writes to its standard input; false when it went away.
    bool write(const std::string& data);
    // Reads what it wrote on its standard output, waiting for something: the bytes read, 0 when it closed its output
    // (ended), -1 on an error.
    long read(char* buffer, std::size_t size);
    // Closes its standard input: it reads the end.
    void closeInput();
    // Closes our end of its output: a read() waiting returns.
    void closeOutput();

    [[nodiscard]] int64_t pid() const noexcept { return pid_; }
    // Still running (not reaped).
    [[nodiscard]] bool running();
    // Waits for its end: its exit code (-1 when killed or unknown).
    int wait();
    // Ends it at once.
    void kill();

   private:
    int64_t pid_ = 0;
    std::optional<int> exit_code_;
#ifdef _WIN32
    void* process_ = nullptr;
    void* in_      = nullptr;  // our end of its standard input
    void* out_     = nullptr;  // our end of its standard output
#else
    int in_  = -1;
    int out_ = -1;
#endif
};

}  // namespace fcg
