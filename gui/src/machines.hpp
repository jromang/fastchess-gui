#pragma once

// The remote machines a tournament can run on: how to reach them over SSH, what was found there (system, cores,
// memory, the fastchess-gui installed), their budgets, the paths of the library's engines there, and the folders
// their books and tablebases are copied to. Kept in machines.json in the profile, without any secret: the key's
// passphrase stays in memory.
//
// And what the machines' commands need, the same for every machine: quoting for the two shells a machine's SSH server
// runs commands with (a POSIX shell, or Windows' cmd), the commands that find out what a machine is, the reading of
// their answers, and the comparison of files here and there.

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include <tour/model.hpp>

#include "ssh_client.hpp"

namespace fcg {

// The version of the channel a tournament's process and the window talk through (`--run --control`, `--attach`),
// which `fastchess-gui --version` prints: a machine whose program speaks another one is offered Install / Update.
inline constexpr int REMOTE_PROTOCOL = 1;

// How a machine's SSH server runs a command.
enum class RemoteShell {
    Unknown,
    Posix,  // Linux, macOS, BSD: the user's shell, which may be fish or csh (commands go through sh -c)
    Cmd,    // Windows' OpenSSH server, its default shell
};

// One argument as each shell reads it. cmdQuote() refuses (nullopt) what cmd cannot take inside quotes: '"', '%' and
// line breaks.
[[nodiscard]] std::string shQuote(std::string_view arg);
[[nodiscard]] std::optional<std::string> cmdQuote(std::string_view arg);
[[nodiscard]] std::optional<std::string> quoteFor(RemoteShell shell, std::string_view arg);
// A POSIX command line, given to sh whatever the user's own shell is.
[[nodiscard]] std::string viaSh(const std::string& command);
// The text of a PowerShell single-quoted string ('it''s').
[[nodiscard]] std::string psQuote(std::string_view text);

// A system as the program names it: os "Linux", "Darwin", "Windows"...; arch "x86_64" or "arm64" (AMD64, amd64,
// aarch64, ARM64 are the same).
struct Platform {
    std::string os;
    std::string arch;
    bool operator==(const Platform&) const = default;
};
[[nodiscard]] Platform normalizePlatform(const std::string& os, const std::string& arch);
[[nodiscard]] Platform thisPlatform();

// What the test of a machine found there.
struct RemoteSystem {
    RemoteShell shell = RemoteShell::Unknown;
    std::string os;       // "Linux", "Darwin", "Windows"
    std::string arch;     // as the machine says it: "x86_64", "AMD64"...
    std::string release;  // the kernel's, or Windows' version
    int threads           = 0;
    uint64_t memory_mb    = 0;
    uint64_t disk_free_mb = 0;  // where the machine's folder is
    std::string home;           // the user's home folder
    std::string fcg_version;    // empty when fastchess-gui is not in the machine's folder
    int fcg_protocol = 0;       // 0 when it does not tell one (older than the remote machines)
    std::string checked;        // when, "2026-11-15 14:03"
};

// The shell from what `echo %OS%` printed: cmd expands it ("Windows_NT"), the others print it as it is.
[[nodiscard]] RemoteShell shellFromEcho(const std::string& output);
// "key=value" lines; the other lines are left out, the last value of a key kept.
[[nodiscard]] std::map<std::string, std::string> keyValues(const std::string& output);
// The version and the protocol `fastchess-gui --version` printed ("fastchess-gui 0.5.9", "protocol 1"); an empty
// version when it is not such an answer.
[[nodiscard]] std::pair<std::string, int> parseVersion(const std::string& output);
// The answers of the probes below, read into `system` (shell and fastchess-gui left as they are).
void readPosixProbe(const std::string& output, RemoteSystem& system);
void readWindowsProbe(const std::string& output, RemoteSystem& system);

// The commands that find out a machine's system, cores, memory and the free space where `folder` is (or would be).
[[nodiscard]] std::string posixProbe(const std::string& folder);
[[nodiscard]] std::optional<std::string> windowsProbe(const std::string& folder);
// The command that prints the SHA-256 of `path` (sha256sum or shasum; certutil on Windows); nullopt when the path
// cannot be quoted for the shell.
[[nodiscard]] std::optional<std::string> hashCommand(RemoteShell shell, const std::string& path);
// The hash those commands printed (the first 64 hexadecimal digits of a line, spaces between them left out), in lower
// case; empty when there is none.
[[nodiscard]] std::string readHash(const std::string& output);
// The command that runs `program` with `args`, from the folder it is in (an engine's own files beside it).
[[nodiscard]] std::optional<std::string> startCommand(RemoteShell shell, const std::string& program,
                                                      const std::string& args = {});
// The command the engine hub starts an engine with: from `dir` (empty: the program's folder), each argument quoted on
// its own, so that the shell runs nothing else; on a POSIX shell the process id is printed on a first line (the
// shell's, which exec hands to the engine), for endProcessCommand(). nullopt when cmd cannot hold an argument.
[[nodiscard]] std::optional<std::string> engineCommand(RemoteShell shell, const std::string& program,
                                                       const std::vector<std::string>& args, const std::string& dir);
// Ends the process `pid` of a POSIX machine: asked to (TERM), then made to a second later (KILL).
[[nodiscard]] std::string endProcessCommand(int64_t pid);

// An engine of the library, on a machine: the program's path there.
struct MachineEngine {
    std::string engine;  // the library's name
    std::string path;    // on the machine
    // The last check: -1 never, 0 failed, 1 passed, with what it found.
    int check_ok = -1;
    std::string check_message;
    std::string id_name;
    // This computer's file copied for this machine instead of the library's program: the build for the machine's
    // system when the library's is for another one (a Windows build for a Windows box, from Linux). Empty: the
    // library's program. Replace and the builds' comparison use it.
    std::string local_build;
    // A folder copied whole with the program (Copy to the machine, with its folder): this computer's, and where it is
    // there. The engine's options naming a file inside it name its copy there. Empty when the program went alone, or
    // its path was given by hand.
    std::string local_dir;
    std::string copied_dir;
    // This computer's build against the program there, as the last comparison found them (the engines' check,
    // Compare, a copy): the SHA-256 of both, and the size and time of this computer's file then, which tell whether
    // it changed since (its CRC is not read: the hash stands for it).
    std::string remote_hash;
    std::string local_hash;
    tour::FileFingerprint local_stamp;

    bool operator==(const MachineEngine& o) const {
        return engine == o.engine && path == o.path && check_ok == o.check_ok && check_message == o.check_message &&
               id_name == o.id_name && local_build == o.local_build && local_dir == o.local_dir &&
               copied_dir == o.copied_dir && remote_hash == o.remote_hash && local_hash == o.local_hash &&
               local_stamp.size == o.local_stamp.size && local_stamp.mtime == o.local_stamp.mtime;
    }
};

// What the comparison of builds says of an engine there: never compared (or nothing to compare), the same as this
// computer's, another one, or this computer's file changed since it was compared (`now`: its stamp today).
enum class BuildMatch { Unknown, Same, Different, Changed };
[[nodiscard]] BuildMatch buildMatch(const MachineEngine& engine, const tour::FileFingerprint& now);

struct Machine {
    std::string id;  // made once, never shown: the setups name the machine by it
    std::string name;
    std::string host;
    int port = 22;
    std::string user;
    SshAuth auth = SshAuth::Agent;
    std::string key_file;  // on this computer
    // The machine's folder for fastchess-gui, its tournaments, books and tablebases: relative to the user's home
    // folder, or absolute.
    std::string folder = "fastchess-gui";
    std::string books_dir;   // empty: <folder>/books
    std::string syzygy_dir;  // empty: <folder>/syzygy
    // A fastchess-gui for the machine's system when it is not this one's (a Windows machine from Linux): a file here.
    std::string program;
    // What tournaments on it may use: 0 for what the test found (its threads but one, 80 % of its memory).
    int cores     = 0;
    int memory_mb = 0;
    std::vector<MachineEngine> engines;
    RemoteSystem system;   // the last test's
    std::string host_key;  // the fingerprint trusted, as the last connection showed it

    [[nodiscard]] std::string booksDir() const;
    [[nodiscard]] std::string syzygyDir() const;
    // fastchess-gui in the machine's folder: fastchess-gui, or fastchess-gui.exe on Windows.
    [[nodiscard]] std::string programPath() const;
    // The budgets in use: the user's, or what the test found.
    [[nodiscard]] int coreBudget() const;
    [[nodiscard]] int memoryBudget() const;
    [[nodiscard]] const MachineEngine* engine(const std::string& name) const;
    // "user@host" (and ":port" when it is not 22).
    [[nodiscard]] std::string address() const;
};

void to_json(nlohmann::json& j, const Machine& m);
void from_json(const nlohmann::json& j, Machine& m);

// machines.json in the profile; an empty list when there is none.
[[nodiscard]] std::vector<Machine> loadMachines(const std::filesystem::path& profile);
bool saveMachines(const std::filesystem::path& profile, const std::vector<Machine>& machines);
// A new machine's id, unlike those of `machines`.
[[nodiscard]] std::string newMachineId(const std::vector<Machine>& machines);
// Where a machine's host keys are kept: the tunnel's file, in the profile.
[[nodiscard]] std::filesystem::path knownHostsFile(const std::filesystem::path& profile);
[[nodiscard]] SshTarget targetOf(const Machine& machine, const std::filesystem::path& profile,
                                 const std::string& passphrase);
// What is missing to connect, in a sentence; empty when the machine can be tried.
[[nodiscard]] std::string incompleteMachine(const Machine& machine);

// The program to install on a machine: this one when the machine's system is this one's, else the one the user chose
// for it. Empty with `error` saying what to do when there is none.
[[nodiscard]] std::filesystem::path programToInstall(const Machine& machine, const std::filesystem::path& self,
                                                     std::string& error);

// ---------------------------------------------------------------------------------------------------------------
// Books and tablebases, here and on a machine.

struct SyncFile {
    std::string name;  // the file name
    uint64_t size = 0;
    std::string hash;  // SHA-256, hexadecimal: books only
    std::filesystem::path local;
    bool operator==(const SyncFile&) const = default;
};

// What a machine's folder lacks of `local`: the files not there, those there of another size (or, `by_hash`, another
// hash), how many are the same and the bytes to send.
struct SyncPlan {
    std::vector<SyncFile> missing;
    std::vector<SyncFile> different;
    std::size_t same = 0;
    uint64_t bytes   = 0;
    [[nodiscard]] bool complete() const noexcept { return missing.empty() && different.empty(); }
};
[[nodiscard]] SyncPlan planSync(const std::vector<SyncFile>& local, const std::vector<SyncFile>& remote, bool by_hash);

// The largest tablebases offered to a machine: 7 pieces take terabytes.
inline constexpr int MAX_PUSHED_PIECES = 6;
// The pieces of a Syzygy file ("KRPvKR.rtbw": 5), 0 when the name is not one.
[[nodiscard]] int syzygyPieces(const std::string& file_name);
// The Syzygy files (.rtbw and .rtbz) of the folders of `paths`, of `max_pieces` at most (0: MAX_PUSHED_PIECES), each
// name once (the first folder's).
[[nodiscard]] std::vector<SyncFile> localSyzygyFiles(const std::string& paths, int max_pieces);
// The same of a machine's folder listing.
[[nodiscard]] std::vector<SyncFile> remoteSyzygyFiles(const std::vector<RemoteEntry>& listing, int max_pieces);

// The SHA-256 of a file, hexadecimal; empty when it cannot be read, or `cancel` turned true.
[[nodiscard]] std::string sha256File(const std::filesystem::path& file, const std::atomic_bool* cancel = nullptr);
// The SHA-256 of `data`, in lower case hexadecimal; empty when the cryptography cannot start.
[[nodiscard]] std::string sha256Hex(std::string_view data);
// The size and modification time of a file, as tour::fingerprint() reads them, without its CRC: what tells that a
// program changed. All zero when it cannot be read.
[[nodiscard]] tour::FileFingerprint fileStamp(const std::filesystem::path& file);
// sha256File(), remembered by file, size and time until the program ends: an engine's program is read once until it
// changes. Called off the UI thread (a program may weigh hundreds of MB); any thread.
[[nodiscard]] std::string cachedSha256(const std::filesystem::path& file, const std::atomic_bool* cancel = nullptr);

// "12 MB", "9.8 GB": a size for the window.
[[nodiscard]] std::string formatBytes(uint64_t bytes);

}  // namespace fcg
