#pragma once

// The web page on the internet: an SSH client (libssh2, on the PSA cryptography of the Mbed TLS the program ships)
// asks a tunnel service for a remote forward, as `ssh -R 80:localhost:PORT nokey@localhost.run` would, and relays
// every connection the service forwards to the web server on this machine. On a thread of its own, reconnecting
// after a failure; its state and the public address are read from any thread.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace fcg {

// The memory libssh2 may hold for a tunnel's connection, unless lowered.
inline constexpr std::size_t TUNNEL_MEMORY = std::size_t(32) << 20;

struct TunnelSettings {
    std::string host = "localhost.run";
    int port         = 22;
    std::string user = "nokey";
    int remote_port  = 80;  // on the server; 0 lets it choose
    int local_port   = 8080;  // the web server's, on this machine
    // The host key's fingerprint ("SHA256:..."), when it is known in advance (localhost.run's). Otherwise the key
    // is trusted the first time, kept in `known_hosts`, and must be the same afterwards.
    std::string pinned;
    std::filesystem::path known_hosts;  // needed when nothing is pinned
    // Timings, shortened by the tests.
    std::chrono::seconds keepalive{30};
    std::chrono::seconds first_retry{2};
    // Limits, lowered by the tests: the memory libssh2 may hold for the connection (beyond, it ends: a server that
    // sends what is never taken, messages nobody asked for), and how long a channel closed on our side counts among
    // the connections while libssh2 waits for the server's close.
    std::size_t max_memory = TUNNEL_MEMORY;
    std::chrono::seconds close_timeout{30};
    // The most one send to the server takes (0: what the socket takes), lowered by the tests: every packet then goes
    // in several sends, each one when libssh2 is called again for it, as on a connection that is often full.
    std::size_t send_limit = 0;

    bool operator==(const TunnelSettings&) const = default;
};

// localhost.run, its RSA host key pinned, for the web server on `local_port`.
[[nodiscard]] TunnelSettings localhostRun(int local_port);

enum class TunnelState {
    Off,
    Connecting,
    OnAir,
    Reconnecting,  // after an error, in a moment
    Failed,        // an error trying again cannot mend (a host key that changed, an authentication refused)
};

struct TunnelStatus {
    TunnelState state = TunnelState::Off;
    std::string url;    // the public address, once known, while on air
    std::string error;  // the last one, while reconnecting or failed
    // When the host key is not the one trusted before: its new fingerprint, to be trusted only by the user.
    std::string new_key;
    int connections = 0;  // relayed at the moment
};

class SshTunnel {
   public:
    SshTunnel() = default;
    ~SshTunnel();
    SshTunnel(const SshTunnel&)            = delete;
    SshTunnel& operator=(const SshTunnel&) = delete;

    // Starts on a thread of its own; another start stops the previous one first.
    void start(TunnelSettings settings);
    // Says goodbye to the server and waits for the thread, a second or two at most: a thread still connecting then
    // ends on its own.
    void stop();
    [[nodiscard]] bool running() const noexcept { return thread_.joinable(); }
    [[nodiscard]] const TunnelSettings& settings() const noexcept { return settings_; }
    [[nodiscard]] TunnelStatus status() const;

   private:
    struct Shared;  // with the thread, which can outlive the tunnel
    static void run(const std::shared_ptr<Shared>& shared);
    static void failed(Shared& shared, const char* what) noexcept;

    TunnelSettings settings_;
    std::shared_ptr<Shared> shared_;
    std::thread thread_;
    friend class TunnelConnection;
};

// "SHA256:..." as ssh-keygen -l shows it, of a host key's blob.
[[nodiscard]] std::string fingerprint(std::string_view key_blob);

// `text` from the server (a reason, a list of methods) made safe to show on a terminal or in the window: control
// characters (C0, DEL, C1) and bytes that are not UTF-8 replaced, so it cannot move the cursor, change colours or
// set a terminal's title.
[[nodiscard]] std::string printableText(std::string_view text);

// The public address a tunnel service printed in `output` (the session's text): the last https:// address on a line
// that tells of the tunnel ("tunneled", "forwarding"), else the first https:// address; none when there is none.
[[nodiscard]] std::optional<std::string> findPublicUrl(std::string_view output);

// A line of libssh2's trace ("[libssh2] 0.000123 Failure Event: -11 - Unable to verify ..."), as the tunnel reads it:
// whether it tells of the server's KEXINIT, and the error it tells of with its text (0 and empty when none). What the
// server sent in it, after the prefix, cannot pass for either.
struct TraceLine {
    bool kexinit = false;
    int error    = 0;
    std::string text;  // 256 bytes at most
};
[[nodiscard]] TraceLine readTrace(std::string_view line);

// The host keys trusted on first use, by "host:port", in `file` (JSON). knownHostKey() gives the one kept for the
// host, empty when there is none or no file yet, and nullopt when the file is there but cannot be read: the keys
// trusted before are unknown then, and none may be trusted in their place. rememberHostKey() leaves such a file as
// it is and returns false, as when it cannot write; an empty fingerprint forgets the host.
[[nodiscard]] std::optional<std::string> knownHostKey(const std::filesystem::path& file, const std::string& host,
                                                      int port);
bool rememberHostKey(const std::filesystem::path& file, const std::string& host, int port,
                     const std::string& fingerprint);

// Why a host key whose fingerprint is `print` may not be trusted: `settings` pins another, or its known hosts kept
// another (`new_key` then, to be trusted only by the user), or cannot be read, or there are none to keep it in, or
// there is no fingerprint at all. nullopt when it may be trusted; trusted on first use, it is kept in the known hosts.
struct HostKeyRefusal {
    std::string reason;
    bool permanent = true;  // trying again cannot mend it
    std::string new_key;
};
[[nodiscard]] std::optional<HostKeyRefusal> checkHostKey(const TunnelSettings& settings, const std::string& print);

// Parses --tunnel's value: "localhost.run" (or empty), or "ssh://user@host[:port][/remote_port]". nullopt and
// `error` when it is neither.
[[nodiscard]] std::optional<TunnelSettings> parseTunnel(const std::string& text, std::string& error);

}  // namespace fcg
