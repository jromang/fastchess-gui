#pragma once

// The web page on the internet: an SSH client of our own (ssh.hpp) asks a tunnel service for a remote forward, as
// `ssh -R 80:localhost:PORT nokey@localhost.run` would, and relays every connection the service forwards to the web
// server on this machine. On a thread of its own, reconnecting after a failure; its state and the public address
// are read from any thread.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace fcg {

struct TunnelSettings {
    std::string host = "localhost.run";
    int port         = 22;
    std::string user = "nokey";
    int remote_port  = 80;  // on the server; 0 lets it choose
    int local_port   = 8080;  // the web server's, on this machine
    // The host key's fingerprint ("SHA256:..."), when it is known in advance (localhost.run's). Otherwise the key
    // is trusted the first time, kept in `known_hosts`, and must be the same afterwards.
    std::string pinned;
    std::filesystem::path known_hosts;  // empty: the key is not kept
    // Timings, shortened by the tests.
    std::chrono::seconds keepalive{30};
    std::chrono::seconds rekey_after{3600};
    uint64_t rekey_bytes = uint64_t(1) << 30;
    std::chrono::seconds first_retry{2};

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
    std::string url;    // the public address, once known
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

    TunnelSettings settings_;
    std::shared_ptr<Shared> shared_;
    std::thread thread_;
    friend class TunnelConnection;
};

// The public address a tunnel service printed in `output` (the session's text): the last https:// address on a line
// that tells of the tunnel ("tunneled", "forwarding"), else the first https:// address; none when there is none.
[[nodiscard]] std::optional<std::string> findPublicUrl(std::string_view output);

// The host keys trusted on first use, by "host:port", in `file` (JSON). An empty fingerprint forgets the host.
[[nodiscard]] std::string knownHostKey(const std::filesystem::path& file, const std::string& host, int port);
void rememberHostKey(const std::filesystem::path& file, const std::string& host, int port,
                     const std::string& fingerprint);

// Parses --tunnel's value: "localhost.run" (or empty), or "ssh://user@host[:port][/remote_port]". nullopt and
// `error` when it is neither.
[[nodiscard]] std::optional<TunnelSettings> parseTunnel(const std::string& text, std::string& error);

}  // namespace fcg
