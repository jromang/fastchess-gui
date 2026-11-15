#pragma once

// A tournament played on a remote machine (machines.hpp), followed by the window as a local one is: its folder there
// (the spec with the machine's paths, the files it starts from), its process started there detached
// (fastchess-gui --run <folder> --detach), the control channel through SSH (fastchess-gui --attach <folder> on an exec
// channel, a Link like a local socket's), connected again after a cut, and its files copied back as they grow, so
// that the window's read-only copy, the Games tab, the ratings and the exports read them here.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <tour/model.hpp>

#include "local_socket.hpp"
#include "machines.hpp"
#include "model.hpp"
#include "ssh_client.hpp"

namespace fcg {

// Where a tournament of this computer plays: machine.json in its folder here. None for a tournament played here.
struct MachinePlace {
    std::string machine;  // the machine's id
    std::string name;     // its name when the tournament started, for the window when the machine is gone
    std::string dir;      // the tournament's folder there
};
[[nodiscard]] std::optional<MachinePlace> readMachinePlace(const std::filesystem::path& dir);
bool writeMachinePlace(const std::filesystem::path& dir, const MachinePlace& place);

// The machine's folder as an absolute path (a relative one is in the user's home folder there, which the test found).
[[nodiscard]] std::string machineFolder(const Machine& machine);
// The folder there of the tournament in `local_dir` here: tournaments/<its folder's name> in the machine's folder.
[[nodiscard]] std::string remoteTournamentDir(const Machine& machine, const std::filesystem::path& local_dir);

// The tournament as the machine plays it: its engines' programs at their paths there (the library's engine of each
// participant, by name or by program), the books in the machine's books folder (a file of the tournament's folder in
// its folder there), the tablebases in its tablebases folder, no PGN file of this computer's. `remote_dir` is the
// tournament's folder there, `local_dir` here. An error naming what is missing, empty on success.
[[nodiscard]] std::string remoteSpec(const tour::TournamentSpec& local, const Machine& machine,
                                     const std::vector<EngineEntry>& library, const std::filesystem::path& local_dir,
                                     const std::string& remote_dir, tour::TournamentSpec& out);

// The files of a remote spec that must be on the machine before it starts: its books (not those of its folder).
[[nodiscard]] std::vector<std::string> requiredBooks(const tour::TournamentSpec& remote, const std::string& remote_dir);

// The commands run there: the tournament started detached, and the control channel of its process.
[[nodiscard]] std::optional<std::string> remoteStartCommand(const Machine& machine, const std::string& remote_dir);
[[nodiscard]] std::optional<std::string> remoteAttachCommand(const Machine& machine, const std::string& remote_dir);

// Prepares the tournament there and starts it (on the link's thread, with its connection): fastchess-gui installed
// in the right version, the engines' programs, books and tablebases there (a book missing stops it, tablebases
// missing are a warning in `notes`), the folder made, the spec written, the files of the folder here copied (those
// of a tournament resumed, unless the machine has them already), then fastchess-gui --run <folder> --detach. False
// with `error`.
bool startOnMachine(SshClient& client, const Machine& machine, const std::filesystem::path& local_dir,
                    const std::string& remote_dir, const std::string& spec_json, std::string& error,
                    std::vector<std::string>& notes);

// What became of the tournament's process there, shared by the links that reach it one after the other.
struct LinkStatus {
    std::atomic_bool gone{false};      // its process is not there any more, or could not start
    std::atomic_bool attached{false};  // the last link reached it
    std::atomic<int> failures{0};      // links that failed in a row (the network, the machine off)
    std::mutex mutex;
    std::string error;               // why, for the window
    std::vector<std::string> notes;  // what its start said (tablebases missing...), taken once
    std::chrono::steady_clock::time_point next_try{};

    void set(std::string text, bool is_gone);
    [[nodiscard]] std::string message();
};

// The control channel of a tournament process on a machine: SSH, then fastchess-gui --attach <folder> there, its
// output read and its input written on a thread of its own. `prepare`, when given, runs first with the connection
// (the tournament's start). The attach is tried again for `patience` while the process starting there makes its
// socket.
class SshLink final : public Link {
   public:
    using Prepare = std::function<bool(SshClient&, std::string& error, std::vector<std::string>& notes)>;
    SshLink(SshTarget target, std::string attach_command, Prepare prepare, std::shared_ptr<LinkStatus> status,
            std::chrono::seconds patience);
    ~SshLink() override;
    SshLink(const SshLink&)            = delete;
    SshLink& operator=(const SshLink&) = delete;

    long read(char* buffer, std::size_t size) override;
    bool write(const std::string& data) override;
    void close() override;

   private:
    void run();
    void end();

    SshTarget target_;
    std::string command_;
    Prepare prepare_;
    std::shared_ptr<LinkStatus> status_;
    std::chrono::seconds patience_;
    std::atomic_bool stop_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::string in_, out_;
    bool ended_ = false;
    std::thread thread_;
};

// The tournament's files copied back from the machine as they grow, on a thread of its own with its own connection:
// the journal, the PGN files, processor.tsv, lichess.json and the engine logs, every few seconds, each from where its
// copy here ends (written afresh when the machine's is shorter).
class MachineSync {
   public:
    MachineSync(SshTarget target, std::string remote_dir, std::filesystem::path local_dir);
    ~MachineSync();
    MachineSync(const MachineSync&)            = delete;
    MachineSync& operator=(const MachineSync&) = delete;

    // A pass now, rather than in a few seconds.
    void now();
    // One last pass, then it stops: the tournament ended there.
    void finish();
    [[nodiscard]] bool finished() const noexcept { return done_; }
    // The last pass's error, empty when it worked.
    [[nodiscard]] std::string error() const;

    // One pass with a connection made: what it copied, in bytes, nullopt with `error` when it failed. For the tests.
    static std::optional<uint64_t> pass(SshClient& client, const std::string& remote_dir,
                                        const std::filesystem::path& local_dir, std::string& error);

   private:
    void run();
    SshTarget target_;
    std::string remote_dir_;
    std::filesystem::path local_dir_;
    std::atomic_bool stop_{false}, finish_{false}, done_{false}, nudge_{false};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::string error_;
    std::thread thread_;
};

}  // namespace fcg
