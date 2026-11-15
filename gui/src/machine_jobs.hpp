#pragma once

// What the window does on a remote machine, each on a thread of its own with a connection of its own: test the
// connection and find out the machine, install or update fastchess-gui there, check the engines' paths, compare the
// books and tablebases a setup uses with the machine's, and copy those missing. The UI thread reads their progress
// and takes their result once they end.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "machines.hpp"
#include "ssh_client.hpp"

namespace fcg {

// A book a setup uses, against the machine's copy.
struct BookCheck {
    std::filesystem::path local;
    std::string remote;  // where it goes on the machine
    uint64_t size                                                   = 0;
    enum class State { Same, Missing, Different, Unreadable } state = State::Missing;
};

// The books and tablebases of a setup, against a machine's.
struct FilesCheck {
    std::vector<BookCheck> books;
    std::string syzygy_dir;        // the machine's
    std::size_t syzygy_local = 0;  // the Syzygy files here, of MAX_PUSHED_PIECES at most
    SyncPlan syzygy;
    std::string error;  // why the check could not be made whole

    // A book is missing or different: a tournament cannot start on the machine.
    [[nodiscard]] bool booksMissing() const;
    // What a push sends: the books missing or different, then the tablebases.
    [[nodiscard]] uint64_t bytesToPush() const;
};

// What a setup needs on a machine: its books (the openings', the participants' own), its tablebase folders here and
// the most pieces its adjudication uses (0: all).
struct FilesRequest {
    std::vector<std::filesystem::path> books;
    std::string syzygy_paths;
    int syzygy_pieces = 0;
};

// What a job found, taken by the UI thread once it ended.
struct JobResult {
    bool ok = false;
    SshError error;
    std::vector<std::string> lines;  // the report, line by line
    std::string host_key;
    bool host_key_new = false;
    std::optional<RemoteSystem> system;
    std::vector<MachineEngine> engines;  // checked
    std::optional<FilesCheck> files;
};

// What a job shows while it runs.
struct JobState {
    std::atomic_bool cancel{false};
    std::atomic<uint64_t> bytes_done{0};
    std::atomic<uint64_t> bytes_total{0};
    // The bytes this job sent itself (bytes_done counts those an earlier copy left too): its pace.
    std::atomic<uint64_t> bytes_sent{0};
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    void setStep(std::string step);
    [[nodiscard]] std::string step() const;

   private:
    mutable std::mutex mutex_;
    std::string step_;
};

class MachineJob {
   public:
    enum class Kind { Test, Install, Engines, Files, Push };
    using Work = std::function<void(SshClient&, JobState&, JobResult&)>;

    // Connects to `target`, then runs `work` with the connection, on a thread of its own.
    MachineJob(Kind kind, SshTarget target, Work work);
    // Cancels it, and waits for its end (a second or two).
    ~MachineJob();
    MachineJob(const MachineJob&)            = delete;
    MachineJob& operator=(const MachineJob&) = delete;

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool finished() const noexcept { return shared_->finished; }
    [[nodiscard]] const JobState& state() const noexcept { return shared_->state; }
    void cancel() { shared_->state.cancel = true; }
    // The result, once finished.
    [[nodiscard]] JobResult take();
    // The time a copy still takes, from its pace so far; nullopt until it can be told.
    [[nodiscard]] std::optional<std::chrono::seconds> remaining() const;

   private:
    struct Shared {
        JobState state;
        JobResult result;
        std::atomic_bool finished{false};
    };
    Kind kind_;
    std::shared_ptr<Shared> shared_;
    std::thread thread_;
};

// The jobs' work, with a connection made: each fills `result` (ok, the report, what it found).

// Finds out the machine: its shell, system, cores, memory, free space, the fastchess-gui in its folder.
void testMachine(SshClient& client, const Machine& machine, JobState& state, JobResult& result);
// Its shell, asked to the machine `name`: cmd, a POSIX shell, or Unknown with `error`.
[[nodiscard]] RemoteShell detectShell(SshClient& client, const std::string& name, std::string& error);
// Copies `program` to the machine's folder and checks it answers --version.
void installProgram(SshClient& client, const Machine& machine, const std::filesystem::path& program, JobState& state,
                    JobResult& result);
// Checks each engine's path on the machine: there, a program, and it answers uci (or xboard, `protocols` giving the
// library's protocol of each engine by name) within `limit`.
void checkEngines(SshClient& client, const Machine& machine, const std::map<std::string, std::string>& protocols,
                  std::chrono::milliseconds limit, JobState& state, JobResult& result);
// Compares the books and tablebases of `request` with the machine's folders.
void checkFiles(SshClient& client, const Machine& machine, const FilesRequest& request, JobState& state,
                JobResult& result);
// Copies what `check` found missing or different, then checks each copy (its size; a book's hash).
void pushFiles(SshClient& client, const Machine& machine, const FilesCheck& check, JobState& state, JobResult& result);

}  // namespace fcg
