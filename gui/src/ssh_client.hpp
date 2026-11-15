#pragma once

// An SSH client for the remote machines (libssh2, on the PSA cryptography of the Mbed TLS the program ships, as the
// broadcast tunnel): logs in with the SSH agent or a key file, runs commands, talks to a program through a channel,
// and copies files with SFTP, resuming a copy cut short. Every call blocks the thread it is made on, never longer
// than the target's timeout without any progress, and gives up at once when asked to: it is made off the UI thread.
// One client, one connection; a client is used by one thread at a time.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fcg {

enum class SshAuth {
    Agent,    // the SSH agent (ssh-agent, Pageant, Windows' OpenSSH agent): the key stays there
    KeyFile,  // a private key file, RSA or Ed25519, its passphrase asked for and kept in memory only
};

struct SshTarget {
    std::string name;  // the machine's, for the messages
    std::string host;
    int port = 22;
    std::string user;
    SshAuth auth = SshAuth::Agent;
    std::filesystem::path key_file;
    std::string passphrase;  // empty when the key has none
    // The host keys trusted on first use (the tunnel's file, see knownHostKey()).
    std::filesystem::path known_hosts;
    // The most a step waits without any progress: the connection, the log in, a command's answer, a file's packet.
    std::chrono::milliseconds timeout{20000};
};

// What went wrong, for the window: a sentence that names the machine and says what to do.
struct SshError {
    enum class Kind {
        None,
        Network,     // the host cannot be reached, or the connection broke
        HostKey,     // the host key changed (new_key), or cannot be checked
        Auth,        // the server refused the key, or no key could be used
        Passphrase,  // the key file needs a passphrase, or it was wrong
        Remote,      // a command or a file operation failed on the machine
        NotFound,    // a remote file or folder is not there
        Timeout,
        Cancelled,
        Local,  // a file on this computer
    };
    Kind kind = Kind::None;
    std::string message;
    std::string new_key;  // HostKey: the fingerprint the server shows now, to be trusted only by the user

    explicit operator bool() const noexcept { return kind != Kind::None; }
};

// What a private key file is, read on this computer before connecting: whether a passphrase must be asked for.
struct KeyFileInfo {
    bool readable  = false;
    bool encrypted = false;
    std::string type;   // "ssh-rsa", "ssh-ed25519"... when the file tells it without its passphrase
    std::string error;  // why it cannot be used, when it cannot
};
[[nodiscard]] KeyFileInfo inspectKeyFile(const std::filesystem::path& file);

// A remote file, as SFTP gives it.
struct RemoteEntry {
    std::string name;  // the file name, without its folder
    uint64_t size  = 0;
    bool directory = false;
    bool regular   = false;
    unsigned perms = 0;  // POSIX permission bits, 0 when the server does not say
    int64_t mtime  = 0;
};

class SshClient {
   public:
    // `cancel`, when given, ends whatever is under way as soon as it turns true.
    explicit SshClient(const std::atomic_bool* cancel = nullptr);
    ~SshClient();
    SshClient(const SshClient&)            = delete;
    SshClient& operator=(const SshClient&) = delete;

    // Connects, checks the host key and logs in. False and error() when it cannot.
    bool connect(const SshTarget& target);
    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] const SshError& error() const noexcept { return error_; }
    // The host key's fingerprint ("SHA256:..."), and whether it was trusted for the first time by this connection.
    [[nodiscard]] const std::string& hostKey() const noexcept { return host_key_; }
    [[nodiscard]] bool hostKeyNew() const noexcept { return host_key_new_; }
    // Says goodbye and closes the connection (the destructor does it too).
    void disconnect();

    // A command run by the machine's shell, its output read whole (`max_output` bytes of each stream at most), `input`
    // written to it first. nullopt and error() when it cannot run or does not end within `limit`.
    struct Result {
        int exit_code = -1;  // -1 when the server does not tell (a signal ended it)
        std::string out, err;
    };
    std::optional<Result> run(const std::string& command, std::chrono::milliseconds limit,
                              const std::string& input = {}, std::size_t max_output = std::size_t(1) << 20);

    // A command whose input and output are read and written as it runs: a program to talk to.
    class Channel {
       public:
        ~Channel();
        Channel(const Channel&)            = delete;
        Channel& operator=(const Channel&) = delete;
        // False when the program is gone or the connection broke.
        bool write(std::string_view data);
        // What the program wrote on its output since the last call, waiting up to `wait` for something. nullopt
        // once it closed its output and nothing is left; error() of the client when the connection broke.
        std::optional<std::string> read(std::chrono::milliseconds wait);
        // Its error output so far, its first 4 KiB: why it could not start, often.
        [[nodiscard]] const std::string& errorOutput() const noexcept { return err_; }
        // No more input: the program reads the end of it.
        void closeInput();
        // Its exit code once it ended, -1 before or when unknown.
        [[nodiscard]] int exitCode();

       private:
        friend class SshClient;
        Channel(SshClient& client, void* channel) : client_(client), channel_(channel) {}
        SshClient& client_;
        void* channel_;  // LIBSSH2_CHANNEL
        bool eof_ = false;
        std::string err_;
    };
    std::unique_ptr<Channel> start(const std::string& command);

    // SFTP. Paths as the server takes them: relative to the user's home, or absolute ("/C:/..." on Windows).
    [[nodiscard]] std::optional<RemoteEntry> stat(const std::string& path);  // nullopt: error(), NotFound if absent
    [[nodiscard]] std::optional<std::vector<RemoteEntry>> list(const std::string& dir);
    // The absolute form of `path`, "." giving the home folder.
    [[nodiscard]] std::optional<std::string> realPath(const std::string& path);
    bool makeDirs(const std::string& path);  // with its parents, those already there left as they are
    bool remove(const std::string& path);    // true when it is gone, or was not there
    bool rename(const std::string& from, const std::string& to);  // `to` replaced when it is there
    bool setPermissions(const std::string& path, unsigned perms);

    // Called with the bytes copied so far (resumed ones included) and the total; false stops the copy.
    using Progress = std::function<bool(uint64_t done, uint64_t total)>;
    // Copies `local` to `remote` through `remote`.part, resumed from what an earlier copy left there, then renamed
    // over `remote` once its size is the local file's. `perms` for the new file (0644 by default).
    bool upload(const std::filesystem::path& local, const std::string& remote, const Progress& progress = {},
                unsigned perms = 0644);
    // The other way, through `local`.part.
    bool download(const std::string& remote, const std::filesystem::path& local, const Progress& progress = {});
    // What `remote` holds from byte `from` on, appended to `local` (written afresh when `from` is 0): a file that grows
    // (a journal, a PGN file), copied as it grows. The bytes copied, nullopt and error() when it fails.
    std::optional<uint64_t> fetch(const std::string& remote, const std::filesystem::path& local, uint64_t from);

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    const std::atomic_bool* cancel_;
    SshError error_;
    std::string host_key_;
    bool host_key_new_ = false;
    friend struct Impl;
};

// A Windows path ("C:\engines\sf.exe", "C:/x") as SFTP names it on Windows' OpenSSH server ("/C:/engines/sf.exe");
// anything else as it is.
[[nodiscard]] std::string sftpPath(const std::string& path);

// The folder part of a remote path and its file name, '/' and '\' both taken as separators.
[[nodiscard]] std::string remoteParent(const std::string& path);
[[nodiscard]] std::string remoteName(const std::string& path);
// `dir` + '/' + `name`, `dir` empty giving `name`.
[[nodiscard]] std::string remoteJoin(const std::string& dir, const std::string& name);

}  // namespace fcg
