#include "ssh_tunnel.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>


#include "console.hpp"
#include "i18n.hpp"
#include "net.hpp"
#include "ssh.hpp"
#include "storage.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;
using namespace ssh;

// localhost.run's RSA host key (ssh-keyscan -t rsa localhost.run | ssh-keygen -lf -). Its Ed25519 key cannot be
// checked with the cryptography at hand, so only RSA is asked for.
constexpr const char* LOCALHOST_RUN_KEY = "SHA256:FV8IMJ4IYjYUTnd6on7PqbRjaZf4c1EhhEBgeUdE94I";

// What is offered, best first. kex-strict-c-v00@openssh.com: the strict key exchange against the Terrapin attack
// (sequence numbers restarted at each new key, nothing else allowed during the first exchange).
constexpr const char* KEX_FIRST   = "curve25519-sha256,curve25519-sha256@libssh.org,kex-strict-c-v00@openssh.com";
constexpr const char* KEX_AGAIN   = "curve25519-sha256,curve25519-sha256@libssh.org";
constexpr const char* HOST_KEYS   = "rsa-sha2-512,rsa-sha2-256";
constexpr const char* CIPHERS     = "aes256-gcm@openssh.com,aes128-gcm@openssh.com,aes256-ctr,aes128-ctr";
constexpr const char* MACS        = "hmac-sha2-256-etm@openssh.com,hmac-sha2-256";
constexpr const char* STRICT_HOST = "kex-strict-s-v00@openssh.com";

// A channel's flow: what the server may send before it waits for us (we take it as fast as the web server does;
// requests are small), and the largest data packet either way.
constexpr uint32_t WINDOW   = 256 * 1024;
constexpr uint32_t MAX_DATA = 32768;
// Connections relayed at once: the web server answers 32 at once and tells the next ones to come back; more than
// that are refused here.
constexpr std::size_t MAX_CHANNELS = 48;
// Bytes waiting to go to the server: beyond, the web server's connections are not read until they went.
constexpr std::size_t OUT_LIMIT = 4 << 20;
// What a message held during a key exchange takes besides its bytes (its string and its place in the queue), about.
constexpr std::size_t HELD_OVERHEAD = 64;
// The session's text kept to read the address in: its last 64 KiB at most (a new address, the domain rotated, comes
// at the end).
constexpr std::size_t MAX_SESSION_TEXT = 64 * 1024;
constexpr auto CONNECT_TIMEOUT   = std::chrono::seconds(15);
constexpr auto HANDSHAKE_TIMEOUT = std::chrono::seconds(30);
constexpr auto MAX_RETRY         = std::chrono::seconds(60);
constexpr int LOCAL_CONNECT_TIMEOUT_MS = 200;

constexpr uint32_t LOCALHOST = 0x7F000001;

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// The first name of `client` that `server` has too (RFC 4253, 7.1).
std::string choose(std::string_view client, std::string_view server) {
    const auto names = [](std::string_view list) {
        std::vector<std::string_view> out;
        while (!list.empty()) {
            const auto comma = list.find(',');
            out.push_back(list.substr(0, comma));
            list.remove_prefix(comma == std::string_view::npos ? list.size() : comma + 1);
        }
        return out;
    };
    const auto offered = names(server);
    for (const auto name : names(client)) {
        if (std::find(offered.begin(), offered.end(), name) != offered.end()) return std::string(name);
    }
    return {};
}

bool hasName(std::string_view list, std::string_view name) { return !choose(name, list).empty(); }

std::string hostPort(const std::string& host, int port) { return host + ":" + std::to_string(port); }

// Wipes a secret when it goes out of scope, however the function that holds it returns.
class Wiped {
   public:
    explicit Wiped(std::string& secret) : secret_(secret) {}
    ~Wiped() { wipe(secret_); }
    Wiped(const Wiped&)            = delete;
    Wiped& operator=(const Wiped&) = delete;

   private:
    std::string& secret_;
};

std::mutex& knownHostsMutex() {
    static std::mutex m;
    return m;
}

}  // namespace

TunnelSettings localhostRun(int local_port) {
    TunnelSettings s;
    s.local_port = local_port;
    s.pinned     = LOCALHOST_RUN_KEY;
    return s;
}

namespace {

// The known hosts: an empty object when there is no file yet, nullopt when it is there but cannot be read or is not
// a JSON object. Read as empty, such a file would let any key in on first use, and be written over, every other host
// forgotten.
std::optional<nlohmann::json> readKnownHosts(const std::filesystem::path& file) {
    std::error_code ec;
    const auto status = std::filesystem::status(file, ec);
    if (status.type() == std::filesystem::file_type::not_found) return nlohmann::json::object();
    if (ec) return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    auto known = nlohmann::json::parse(in, nullptr, false);
    if (known.is_discarded() || !known.is_object()) return std::nullopt;
    return known;
}

}  // namespace

std::optional<std::string> knownHostKey(const std::filesystem::path& file, const std::string& host, int port) {
    std::lock_guard<std::mutex> lock(knownHostsMutex());
    const auto known = readKnownHosts(file);
    if (!known) return std::nullopt;
    // A value that is not a string is no key.
    const auto it = known->find(hostPort(host, port));
    return it != known->end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool rememberHostKey(const std::filesystem::path& file, const std::string& host, int port,
                     const std::string& fingerprint) {
    std::lock_guard<std::mutex> lock(knownHostsMutex());
    auto known = readKnownHosts(file);
    if (!known) return false;
    if (fingerprint.empty()) {
        known->erase(hostPort(host, port));
    } else {
        (*known)[hostPort(host, port)] = fingerprint;
    }
    return writeJson(file, *known);
}

std::string printableText(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            out += c == '\t' || c == '\n' || c == '\r' ? ' ' : c < 0x20 || c == 0x7F ? '?' : static_cast<char>(c);
            ++i;
            continue;
        }
        // A whole UTF-8 sequence, in its shortest form, of a character that is not a surrogate: kept, unless it is a
        // C1 control (U+0080 to U+009F, CSI among them). Anything else, one byte at a time: replaced.
        const std::size_t n = c >= 0xC2 && c <= 0xDF ? 2 : c >= 0xE0 && c <= 0xEF ? 3 : c >= 0xF0 && c <= 0xF4 ? 4 : 0;
        uint32_t code       = n == 2 ? c & 0x1F : n == 3 ? c & 0x0F : c & 0x07;
        bool whole          = n != 0 && i + n <= text.size();
        for (std::size_t k = 1; whole && k < n; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            whole           = (next & 0xC0) == 0x80;
            code            = code << 6 | (next & 0x3F);
        }
        constexpr uint32_t SHORTEST[] = {0, 0, 0x80, 0x800, 0x10000};
        if (!whole || code < SHORTEST[n] || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
            out += '?';
            ++i;
            continue;
        }
        if (code <= 0x9F) {
            out += '?';
        } else {
            out.append(text.substr(i, n));
        }
        i += n;
    }
    return out;
}

std::optional<std::string> findPublicUrl(std::string_view output) {
    std::optional<std::string> first, told;
    std::size_t line_start = 0;
    while (line_start < output.size()) {
        auto line_end = output.find('\n', line_start);
        if (line_end == std::string_view::npos) line_end = output.size();
        const auto line  = output.substr(line_start, line_end - line_start);
        const auto words = lower(line);
        const bool tells = words.find("tunneled") != std::string::npos || words.find("forwarding") != std::string::npos;
        for (auto at = words.find("https://"); at != std::string::npos; at = words.find("https://", at + 8)) {
            // The host and its port only: letters, digits, dots and dashes, nothing a page could be told to do with it.
            std::size_t end = at + 8;
            while (end < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '.' || line[end] == '-')) {
                ++end;
            }
            const auto host = line.substr(at + 8, end - at - 8);
            if (host.find('.') == std::string_view::npos || host.front() == '.' || host.back() == '.') continue;
            auto url = "https://" + lower(host);
            // A port, when there is one: 1 to 65535, digits only (an address with another is not taken).
            if (end < line.size() && line[end] == ':') {
                std::size_t digits = end + 1;
                while (digits < line.size() && digits - end <= 6 &&
                       std::isdigit(static_cast<unsigned char>(line[digits]))) {
                    ++digits;
                }
                const auto port = line.substr(end + 1, digits - end - 1);
                if (!port.empty()) {
                    if (port.size() > 5 || port.front() == '0' || std::stoi(std::string(port)) > 65535) continue;
                    url += ":" + std::string(port);
                }
            }
            if (!first) first = url;
            if (tells) told = url;
        }
        line_start = line_end + 1;
    }
    return told ? told : first;
}

std::optional<TunnelSettings> parseTunnel(const std::string& text, std::string& error) {
    if (text.empty() || text == "localhost.run") return localhostRun(0);
    if (text.rfind("ssh://", 0) != 0) {
        error = "--tunnel takes localhost.run or ssh://user@host[:port][/remote_port], not " + text;
        return std::nullopt;
    }
    TunnelSettings s;
    std::string rest = text.substr(6);
    const auto number = [](const std::string& digits, int& out, int low) {
        if (digits.empty() || digits.size() > 5 ||
            !std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            return false;
        }
        out = std::stoi(digits);
        return out >= low && out <= 65535;
    };
    if (const auto slash = rest.find('/'); slash != std::string::npos) {
        if (!number(rest.substr(slash + 1), s.remote_port, 0)) {
            error = "--tunnel: not a remote port: " + rest.substr(slash + 1);
            return std::nullopt;
        }
        rest.erase(slash);
    }
    const auto at = rest.rfind('@');
    if (at == std::string::npos || at == 0) {
        error = "--tunnel: ssh://user@host, the user is missing";
        return std::nullopt;
    }
    s.user = rest.substr(0, at);
    rest.erase(0, at + 1);
    if (const auto colon = rest.rfind(':'); colon != std::string::npos) {
        if (!number(rest.substr(colon + 1), s.port, 1)) {
            error = "--tunnel: not a port: " + rest.substr(colon + 1);
            return std::nullopt;
        }
        rest.erase(colon);
    }
    if (rest.empty()) {
        error = "--tunnel: ssh://user@host, the host is missing";
        return std::nullopt;
    }
    s.host = rest;
    return s;
}

struct SshTunnel::Shared {
    TunnelSettings settings;
    std::atomic_bool stopping{false};
    std::atomic_bool finished{false};
    std::mutex mutex;  // what follows
    TunnelStatus status;

    void set(TunnelState state, const std::string& error = {}) {
        std::lock_guard<std::mutex> lock(mutex);
        status.state = state;
        status.error = error;
        // Off the air, the address is no longer ours: a service may give it to someone else meanwhile.
        if (state != TunnelState::OnAir) {
            status.connections = 0;
            status.url.clear();
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------
// One connection to the server, from the version exchange to its end.

class TunnelConnection {
   public:
    TunnelConnection(SshTunnel::Shared& shared, const TunnelSettings& settings)
        : tunnel_(shared), cfg_(settings), stopping_(shared.stopping) {}
    ~TunnelConnection() {
        for (auto& key : next_rx_) wipe(key);
        wipe(session_id_);
    }
    TunnelConnection(const TunnelConnection&)            = delete;
    TunnelConnection& operator=(const TunnelConnection&) = delete;

    struct End {
        std::string reason;  // empty when stopped
        bool permanent = false;
        std::string new_key;
        bool was_on_air = false;
    };

    End run() {
        if (!startCrypto()) {
            fail("the cryptography cannot start", true);
            return end_;
        }
        std::string error;
        // Given up on as soon as the tunnel stops: only finding the host's address cannot be cut short.
        const auto fd =
            net::connectHost(cfg_.host, std::to_string(cfg_.port),
                             static_cast<int>(std::chrono::milliseconds(CONNECT_TIMEOUT).count()), error, &stopping_);
        if (!fd) {
            fail(error);
            return end_;
        }
        socket_  = std::make_unique<net::Socket>(*fd);
        started_ = Clock::now();
        heard_   = started_;
        v_c_     = std::string("SSH-2.0-fastchessgui_") + FCG_VERSION;
        out_     = v_c_ + "\r\n";
        startKex();

        while (!stopping_ && end_.reason.empty()) {
            step();
            timers();
        }
        if (stopping_ && end_.reason.empty()) goodbye();
        return end_;
    }

   private:
    struct Channel {
        uint32_t peer = 0;
        std::unique_ptr<net::Socket> socket;  // the web server's connection; none for the session
        bool confirmed       = false;         // the session's, by the server
        uint32_t peer_window = 0;             // what we may still send
        uint32_t peer_max    = 0;
        uint32_t window      = WINDOW;  // what the server may still send
        uint32_t consumed    = 0;       // taken since the last adjustment
        std::string to_local;           // for the web server
        bool eof_in         = false;    // the server sends no more
        bool shut           = false;    // and the web server was told
        bool close_sent     = false;
        bool close_received = false;
        bool forwarded      = false;  // a viewer's connection, not the session
        Clock::time_point closed_at;  // when close_sent
    };

    enum class Phase { Version, Kex, Auth, Forward, OnAir };
    enum class Reply { Forward, Keepalive };

    void fail(const std::string& reason, bool permanent = false) {
        if (!end_.reason.empty()) return;
        // Server text is in many reasons (DISCONNECT's, accepted before the keys; USERAUTH_FAILURE's methods): it
        // goes to the window, the Console, and a headless run's terminal.
        end_.reason    = printableText(reason);
        end_.permanent = permanent;
    }

    // Sends a message, or keeps it for after the key exchange: nothing but its own messages goes between our
    // KEXINIT and our NEWKEYS (RFC 4253, 7.1).
    void send(std::string payload) {
        const auto type = static_cast<uint8_t>(payload[0]);
        const bool transport = type <= DEBUG_TEXT || (type >= KEXINIT && type < USERAUTH_REQUEST);
        if (kexinit_sent_ && !newkeys_sent_ && !transport) {
            held_bytes_ += payload.size() + HELD_OVERHEAD;
            held_.push_back(std::move(payload));
        } else {
            bytes_ += payload.size();
            out_ += tx_.seal(payload, seq_tx_++);
        }
        // Our sends are bounded (the web server's connections are not read beyond OUT_LIMIT): more is a server that
        // reads nothing, or asks for answers while it keeps a key exchange open.
        if (out_.size() + held_bytes_ > cfg_.max_pending) fail("the server does not take what is sent to it");
    }

    // The socket: what came, what goes, every channel's connection.
    void step() {
        std::vector<net::Wait> waits;
        waits.push_back({socket_->handle(), true, !out_.empty()});
        std::vector<Channel*> relayed;
        const bool can_read_local = !(kexinit_sent_ && !newkeys_sent_) && out_.size() < OUT_LIMIT;
        for (auto& [id, ch] : channels_) {
            if (!ch.socket || ch.close_sent) continue;
            relayed.push_back(&ch);
            waits.push_back({ch.socket->handle(), can_read_local && ch.peer_window > 0, !ch.to_local.empty()});
        }
        net::waitAny(waits, 100);

        if (waits[0].can_read) {
            const auto n = socket_->receive(in_, 65536);
            if (n < 0) return fail(phase_ == Phase::OnAir ? "the server closed the connection"
                                                          : "the server closed the connection before the tunnel opened");
            // Nothing more is handled once the tunnel stops (see stop()).
            if (n > 0 && !stopping_) readPackets();
        }
        if (!end_.reason.empty()) return;
        if (waits[0].can_write && !out_.empty()) {
            const auto n = socket_->send(out_);
            if (n < 0) return fail("the connection to the server was lost");
            out_.erase(0, static_cast<std::size_t>(n));
        }
        for (std::size_t i = 0; i < relayed.size(); ++i) relay(*relayed[i], waits[i + 1]);
        // The channels closed both ways are gone; their numbers can serve again.
        for (auto it = channels_.begin(); it != channels_.end();) {
            it = it->second.close_sent && it->second.close_received ? channels_.erase(it) : std::next(it);
        }
        int count = 0;
        for (const auto& [id, ch] : channels_) count += ch.socket ? 1 : 0;
        if (count != relayed_count_) {
            relayed_count_ = count;
            std::lock_guard<std::mutex> lock(tunnel_.mutex);
            tunnel_.status.connections = count;
        }
    }

    void relay(Channel& ch, const net::Wait& wait) {
        if (!ch.socket) return;  // closed by the server meanwhile
        if (wait.can_write && !ch.to_local.empty()) {
            const auto n = ch.socket->send(ch.to_local);
            if (n < 0) return closeChannel(ch);
            ch.to_local.erase(0, static_cast<std::size_t>(n));
            consume(ch, static_cast<uint32_t>(n));
        }
        if (ch.eof_in && ch.to_local.empty() && !ch.shut) {
            ch.socket->shutdownWrite();
            ch.shut = true;
        }
        if (wait.can_read && ch.peer_window > 0) {
            std::string data;
            const auto n = ch.socket->receive(data, std::min(ch.peer_window, ch.peer_max));
            if (n < 0) return closeChannel(ch);  // the web server answered, and closed
            if (n > 0) {
                ch.peer_window -= static_cast<uint32_t>(n);
                send(Writer().byte(CHANNEL_DATA).u32(ch.peer).string(data).take());
            }
        }
    }

    // What was given on: the server may send as much again.
    void consume(Channel& ch, uint32_t n) {
        ch.consumed += n;
        if (ch.consumed >= WINDOW / 2) {
            send(Writer().byte(CHANNEL_WINDOW_ADJUST).u32(ch.peer).u32(ch.consumed).take());
            ch.window += ch.consumed;
            ch.consumed = 0;
        }
    }

    void closeChannel(Channel& ch) {
        if (ch.close_sent) return;
        send(Writer().byte(CHANNEL_EOF).u32(ch.peer).take());
        send(Writer().byte(CHANNEL_CLOSE).u32(ch.peer).take());
        ch.close_sent = true;
        ch.closed_at  = Clock::now();
        ch.to_local.clear();
        ch.socket.reset();
    }

    void timers() {
        const auto now = Clock::now();
        // The channels we closed and the server did not: forgotten after a while (their numbers are not given again
        // before 2^32 more).
        for (auto it = channels_.begin(); it != channels_.end();) {
            const auto& ch = it->second;
            it = ch.close_sent && !ch.close_received && now - ch.closed_at > cfg_.close_timeout ? channels_.erase(it)
                                                                                                : std::next(it);
        }
        if (phase_ != Phase::OnAir) {
            if (now - started_ > HANDSHAKE_TIMEOUT) fail("no answer from the server in time");
            return;
        }
        if (now - heard_ > cfg_.keepalive * 3 + std::chrono::seconds(5)) {
            return fail("the server stopped answering");
        }
        if (now - last_keepalive_ >= cfg_.keepalive) {
            last_keepalive_ = now;
            send(Writer().byte(GLOBAL_REQUEST).string("keepalive@openssh.com").boolean(true).take());
            replies_.push_back(Reply::Keepalive);
        }
        // New keys from time to time, if the server does not ask first.
        if (!kexinit_sent_ && (now - keyed_at_ >= cfg_.rekey_after || bytes_ >= cfg_.rekey_bytes)) startKex();
    }

    void goodbye() {
        if (session_id_.empty() || !socket_) return;
        send(Writer().byte(DISCONNECT).u32(11).string("bye").string("").take());
        const auto until = Clock::now() + std::chrono::seconds(1);
        while (!out_.empty() && Clock::now() < until) {
            std::vector<net::Wait> waits{{socket_->handle(), false, true}};
            net::waitAny(waits, 100);
            if (!waits[0].can_write) continue;
            const auto n = socket_->send(out_);
            if (n < 0) break;
            out_.erase(0, static_cast<std::size_t>(n));
        }
    }

    void readPackets() {
        if (phase_ == Phase::Version && !readVersion()) return;
        while (end_.reason.empty()) {
            std::string payload;
            const auto r = rx_.open(in_, seq_rx_, payload);
            if (r == PacketCipher::Open::More) return;
            if (r == PacketCipher::Open::Bad || payload.empty()) return fail("the server sent a packet that is not valid");
            const auto seq = seq_rx_++;
            bytes_ += payload.size();
            heard_ = Clock::now();
            handle(payload, seq);
        }
    }

    // The server's version line, after any other lines it sends first (RFC 4253, 4.2).
    bool readVersion() {
        while (true) {
            const auto eol = in_.find('\n');
            if (eol == std::string::npos) {
                if (in_.size() > 255) fail("the server is not an SSH server");
                return false;
            }
            auto line = in_.substr(0, eol);
            in_.erase(0, eol + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("SSH-", 0) == 0) {
                if (line.rfind("SSH-2.0-", 0) != 0 && line.rfind("SSH-1.99-", 0) != 0) {
                    fail("the server does not speak SSH 2", true);
                    return false;
                }
                v_s_   = line;
                phase_ = Phase::Kex;
                return true;
            }
            if (++lines_before_ > 50) {
                fail("the server is not an SSH server");
                return false;
            }
        }
    }

    void handle(const std::string& payload, uint32_t seq) {
        const auto type = static_cast<uint8_t>(payload[0]);
        Reader r(std::string_view(payload).substr(1));
        // The first key exchange: nothing but its messages until the server's NEWKEYS. With the strict exchange
        // (OpenSSH's PROTOCOL file), not even IGNORE or DEBUG; its KEXINIT, the first packet, is checked there.
        if (!initial_kex_done_) {
            const bool kex     = type == KEXINIT || type == KEX_ECDH_REPLY || type == NEWKEYS;
            const bool allowed = kex || type == DISCONNECT || (!strict_ && (type == IGNORE_DATA || type == DEBUG_TEXT));
            if (!allowed) {
                return fail(strict_ ? "the server broke the strict key exchange"
                                    : "the server sent a message before the key exchange ended");
            }
        }
        switch (type) {
            case DISCONNECT: {
                r.u32();
                const auto why = r.string(1024);
                return fail("the server ended the connection: " + std::string(why));
            }
            case IGNORE_DATA:
            case DEBUG_TEXT:
            case UNIMPLEMENTED:
            case USERAUTH_BANNER:
            case EXT_INFO:
            case CHANNEL_SUCCESS:
            case CHANNEL_FAILURE:
                return;
            case KEXINIT:
                return onKexInit(payload, seq);
            case KEX_ECDH_REPLY:
                return onEcdhReply(r);
            case NEWKEYS:
                return onNewKeys();
            case SERVICE_ACCEPT:
                if (phase_ != Phase::Auth) return fail("the server sent an unexpected message");
                send(Writer().byte(USERAUTH_REQUEST).string(cfg_.user).string("ssh-connection").string("none").take());
                return;
            case USERAUTH_SUCCESS:
                if (phase_ != Phase::Auth) return fail("the server sent an unexpected message");
                phase_ = Phase::Forward;
                send(Writer()
                         .byte(GLOBAL_REQUEST)
                         .string("tcpip-forward")
                         .boolean(true)
                         .string("localhost")
                         .u32(static_cast<uint32_t>(cfg_.remote_port))
                         .take());
                replies_.push_back(Reply::Forward);
                return;
            case USERAUTH_FAILURE: {
                const auto methods = r.string(1024);
                return fail(trf("{} does not let user \"{}\" in without a key or a password (it accepts: {})",
                                cfg_.host, cfg_.user, std::string(methods)),
                            true);
            }
            case REQUEST_SUCCESS:
            case REQUEST_FAILURE:
                return onReply(type == REQUEST_SUCCESS, r);
            case GLOBAL_REQUEST: {
                r.string(256);
                if (r.boolean()) send(Writer().byte(REQUEST_FAILURE).take());
                return;
            }
            case CHANNEL_OPEN:
                return onChannelOpen(r);
            default:
                break;
        }
        if (type >= CHANNEL_OPEN_CONFIRMATION && type <= CHANNEL_FAILURE) return onChannel(type, r);
        send(Writer().byte(UNIMPLEMENTED).u32(seq).take());
    }

    // --- Key exchange (RFC 4253, 7 and 8; RFC 8731) ---

    void startKex() {
        Writer w;
        w.byte(KEXINIT).raw(randomBytes(16));
        w.string(session_id_.empty() ? KEX_FIRST : KEX_AGAIN).string(HOST_KEYS);
        w.string(CIPHERS).string(CIPHERS).string(MACS).string(MACS).string("none").string("none");
        w.string("").string("").boolean(false).u32(0);
        i_c_          = w.take();
        kexinit_sent_ = true;
        newkeys_sent_ = false;
        send(i_c_);
    }

    void onKexInit(const std::string& payload, uint32_t seq) {
        if (!i_s_.empty()) return fail("the server started a key exchange twice");
        i_s_ = payload;
        if (!kexinit_sent_) startKex();
        Reader r(std::string_view(payload).substr(1));
        r.raw(16);  // the cookie
        std::string lists[10];
        for (auto& list : lists) list = std::string(r.string(8192));
        r.boolean();
        r.u32();
        if (!r.ok()) return fail("the server's key exchange cannot be read");
        if (!initial_kex_done_) {
            strict_ = hasName(lists[0], STRICT_HOST);
            // Strict: the server's first packet, or a message slipped in before it could shift the sequence numbers.
            if (strict_ && seq != 0) return fail("the server broke the strict key exchange");
        }

        kex_      = choose(KEX_AGAIN, lists[0]);
        host_alg_ = choose(HOST_KEYS, lists[1]);
        cipher_c_ = choose(CIPHERS, lists[2]);
        cipher_s_ = choose(CIPHERS, lists[3]);
        mac_c_    = PacketCipher::aead(cipher_c_) ? std::string() : choose(MACS, lists[4]);
        mac_s_    = PacketCipher::aead(cipher_s_) ? std::string() : choose(MACS, lists[5]);
        const bool macs = (PacketCipher::aead(cipher_c_) || !mac_c_.empty()) &&
                          (PacketCipher::aead(cipher_s_) || !mac_s_.empty());
        if (kex_.empty() || host_alg_.empty() || cipher_c_.empty() || cipher_s_.empty() || !macs ||
            !hasName(lists[6], "none") || !hasName(lists[7], "none")) {
            return fail(trf("{} has no algorithm in common with fastchess-gui (it needs curve25519-sha256, an RSA "
                            "host key and AES)",
                            cfg_.host),
                        true);
        }
        ecdh_ = std::make_unique<X25519>();
        if (!ecdh_->ok()) return fail("the cryptography cannot make a key");
        send(Writer().byte(KEX_ECDH_INIT).string(ecdh_->publicKey()).take());
    }

    void onEcdhReply(Reader& r) {
        if (!ecdh_ || i_s_.empty() || newkeys_sent_) return fail("the server sent an unexpected message");
        const std::string key_blob(r.string(4096));
        const std::string q_s(r.string(64));
        const std::string signature(r.string(4096));
        if (!r.ok()) return fail("the server's key exchange cannot be read");

        // The host key, at the first exchange: compared with the one pinned or trusted before, then its signature
        // checked, and only then trusted or offered to the user; at the next ones, the same as at the first.
        auto trust = HostKey::Trusted;
        if (session_id_.empty()) {
            trust = checkHostKey(key_blob);
            if (trust == HostKey::Refused) return;
        } else if (key_blob != host_key_) {
            return fail(trf("{} changed its host key while connected", cfg_.host), true);
        }
        auto secret = ecdh_->agree(q_s);
        if (!secret) return fail("the server's key exchange is not valid");
        auto k = Writer().mpint(*secret).take();
        wipe(*secret);
        Writer exchange;
        exchange.string(v_c_)
            .string(v_s_)
            .string(i_c_)
            .string(i_s_)
            .string(key_blob)
            .string(ecdh_->publicKey())
            .string(q_s)
            .raw(k);
        auto h      = sha256(exchange.data());
        auto hashed = exchange.take();
        wipe(hashed);
        // K and H, and the keys made of them, are wiped however this ends; the first H stays, as the session id.
        const Wiped wipe_k(k), wipe_h(h);
        if (const auto bad = verifyRsa(key_blob, signature, h, host_alg_); !bad.empty()) {
            return fail(trf("{}: {}", cfg_.host, bad), true);
        }
        if (trust == HostKey::Changed) {
            const auto print = fingerprint(key_blob);
            end_.new_key     = print;
            return fail(trf("The host key of {} changed: {} now, {} before. Someone may be in the way; trust the new "
                            "key only if the server's owner says it changed.",
                            cfg_.host, print, known_print_),
                        true);
        }
        if (trust == HostKey::New) keepHostKey(key_blob);
        if (session_id_.empty()) {
            host_key_   = key_blob;
            session_id_ = h;
        }
        const auto derive = [&](char letter, std::size_t size) { return deriveKey(k, h, letter, session_id_, size); };

        send(Writer().byte(NEWKEYS).take());
        auto key = derive('C', PacketCipher::keySize(cipher_c_)), iv = derive('A', PacketCipher::ivSize(cipher_c_)),
             mac_key     = derive('E', PacketCipher::macKeySize(mac_c_));
        const auto error = tx_.start(cipher_c_, mac_c_, key, iv, mac_key);
        wipe(key);
        wipe(iv);
        wipe(mac_key);
        if (!error.empty()) return fail(error);
        if (strict_) seq_tx_ = 0;
        next_rx_      = {derive('D', PacketCipher::keySize(cipher_s_)), derive('B', PacketCipher::ivSize(cipher_s_)),
                         derive('F', PacketCipher::macKeySize(mac_s_))};
        newkeys_sent_ = true;
        ecdh_.reset();
        while (!held_.empty()) {
            auto payload = std::move(held_.front());
            held_.pop_front();
            held_bytes_ -= std::min(held_bytes_, payload.size() + HELD_OVERHEAD);
            send(std::move(payload));
        }
    }

    void onNewKeys() {
        if (!newkeys_sent_) return fail("the server sent an unexpected message");
        if (const auto error = rx_.start(cipher_s_, mac_s_, next_rx_[0], next_rx_[1], next_rx_[2]); !error.empty()) {
            return fail(error);
        }
        for (auto& key : next_rx_) wipe(key);
        if (strict_) seq_rx_ = 0;
        initial_kex_done_ = true;
        kexinit_sent_ = false;
        newkeys_sent_ = false;
        i_c_.clear();
        i_s_.clear();
        keyed_at_ = Clock::now();
        bytes_    = 0;
        if (phase_ == Phase::Kex) {
            phase_ = Phase::Auth;
            send(Writer().byte(SERVICE_REQUEST).string("ssh-userauth").take());
        }
    }

    // The host key against the one pinned, or the one trusted the first time. Refused: the connection failed.
    enum class HostKey { Trusted, New, Changed, Refused };
    HostKey checkHostKey(const std::string& blob) {
        const auto print = fingerprint(blob);
        if (!cfg_.pinned.empty()) {
            if (print == cfg_.pinned) return HostKey::Trusted;
            fail(trf("The host key of {} is not the one expected ({}, instead of {}): someone may be in the way. "
                     "Nothing was sent.",
                     cfg_.host, print, cfg_.pinned),
                 true);
            return HostKey::Refused;
        }
        // Nowhere to keep it: not trusted blindly, every time.
        if (cfg_.known_hosts.empty()) {
            fail("the host key of " + cfg_.host + " cannot be checked: neither pinned nor kept", true);
            return HostKey::Refused;
        }
        const auto known = knownHostKey(cfg_.known_hosts, cfg_.host, cfg_.port);
        if (!known) {
            fail(trf("{} cannot be read, so the host keys trusted before are unknown: nothing was sent to {}. Repair "
                     "the file, or delete it to trust the server's key again.",
                     cfg_.known_hosts.string(), cfg_.host),
                 true);
            return HostKey::Refused;
        }
        if (known->empty()) return HostKey::New;
        if (*known == print) return HostKey::Trusted;
        known_print_ = *known;
        return HostKey::Changed;
    }

    // A key seen for the first time, whose signature was checked: trusted from now on.
    void keepHostKey(const std::string& blob) {
        const auto print = fingerprint(blob);
        if (!rememberHostKey(cfg_.known_hosts, cfg_.host, cfg_.port, print)) {
            Console::instance().add(trf("Tunnel: the host key of {} ({}) could not be kept in {}", cfg_.host, print,
                                        cfg_.known_hosts.string()),
                                    true);
            return;
        }
        Console::instance().add(trf("Tunnel: the host key of {} is {}, trusted from now on", cfg_.host, print));
    }

    // --- Connection protocol (RFC 4254) ---

    void onReply(bool success, Reader& r) {
        if (replies_.empty()) return fail("the server sent an unexpected message");
        const auto kind = replies_.front();
        replies_.pop_front();
        if (kind == Reply::Keepalive) return;
        if (!success) {
            return fail(trf("{} refused to forward its port {}", cfg_.host, cfg_.remote_port));
        }
        bound_port_ = cfg_.remote_port;
        if (cfg_.remote_port == 0 && r.left() >= 4) bound_port_ = static_cast<int>(r.u32());
        phase_          = Phase::OnAir;
        last_keepalive_ = Clock::now();
        end_.was_on_air = true;
        {
            std::lock_guard<std::mutex> lock(tunnel_.mutex);
            tunnel_.status.state = TunnelState::OnAir;
            tunnel_.status.error.clear();
        }
        // The services print the address on a session: one, with a shell, as `ssh -R` would open.
        session_ = nextId();
        auto& ch = channels_[session_];
        (void)ch;
        send(Writer().byte(CHANNEL_OPEN).string("session").u32(session_).u32(WINDOW).u32(MAX_DATA).take());
        // Until it does, or for a server of one's own that prints nothing: the server and the port.
        if (cfg_.pinned.empty()) {
            setUrl(bound_port_ == 80 ? "http://" + cfg_.host
                                     : "http://" + cfg_.host + ":" + std::to_string(bound_port_));
        }
    }

    void setUrl(const std::string& url) {
        std::lock_guard<std::mutex> lock(tunnel_.mutex);
        if (tunnel_.status.url == url) return;
        tunnel_.status.url = url;
        Console::instance().add(trf("Tunnel: the web page is on {}", url));
    }

    uint32_t nextId() {
        while (channels_.count(next_id_)) ++next_id_;
        return next_id_++;
    }

    void onChannelOpen(Reader& r) {
        const auto type   = r.string(64);
        const auto sender = r.u32();
        const auto window = r.u32();
        const auto max    = r.u32();
        r.string(256);  // the address connected to, and its port
        r.u32();
        r.string(256);  // where the connection comes from
        r.u32();
        if (!r.ok()) return fail("the server sent a message that is not valid");
        const auto refuse = [&](uint32_t code, const char* why) {
            send(Writer().byte(CHANNEL_OPEN_FAILURE).u32(sender).u32(code).string(why).string("").take());
        };
        if (type != "forwarded-tcpip" || phase_ != Phase::OnAir) return refuse(1, "not accepted");
        // Those closed on our side that the server did not close yet count too: a server that never closes them
        // must not have more and more.
        const auto open =
            std::count_if(channels_.begin(), channels_.end(), [](const auto& c) { return c.second.forwarded; });
        if (static_cast<std::size_t>(open) >= MAX_CHANNELS) return refuse(4, "too many connections");
        // Every channel waits meanwhile: a web server that does not answer at once is not there.
        const auto fd = net::connectTo(LOCALHOST, static_cast<uint16_t>(cfg_.local_port), LOCAL_CONNECT_TIMEOUT_MS);
        if (!fd) return refuse(2, "the web page is not served");
        const auto id    = nextId();
        auto& ch         = channels_[id];
        ch.peer          = sender;
        ch.forwarded     = true;
        ch.socket        = std::make_unique<net::Socket>(*fd);
        ch.peer_window   = window;
        ch.peer_max      = std::clamp<uint32_t>(max, 1, MAX_DATA);
        ++relayed_count_;
        send(Writer().byte(CHANNEL_OPEN_CONFIRMATION).u32(sender).u32(id).u32(WINDOW).u32(MAX_DATA).take());
    }

    void onChannel(uint8_t type, Reader& r) {
        const auto id = r.u32();
        const auto it = channels_.find(id);
        if (!r.ok()) return fail("the server sent a message that is not valid");
        if (it == channels_.end()) return;  // closed meanwhile
        auto& ch = it->second;
        switch (type) {
            case CHANNEL_OPEN_CONFIRMATION: {
                ch.peer        = r.u32();
                ch.peer_window = r.u32();
                ch.peer_max    = std::clamp<uint32_t>(r.u32(), 1, MAX_DATA);
                ch.confirmed   = true;
                send(Writer().byte(CHANNEL_REQUEST).u32(ch.peer).string("shell").boolean(false).take());
                return;
            }
            case CHANNEL_OPEN_FAILURE:
                // No session: no address printed, the tunnel works all the same.
                ch.close_sent = ch.close_received = true;
                return;
            case CHANNEL_WINDOW_ADJUST: {
                const auto more = r.u32();
                ch.peer_window  = static_cast<uint32_t>(std::min<uint64_t>(uint64_t(ch.peer_window) + more, 0xFFFFFFFFu));
                return;
            }
            case CHANNEL_DATA:
            case CHANNEL_EXTENDED_DATA: {
                if (type == CHANNEL_EXTENDED_DATA) r.u32();
                const auto data = r.string(MAX_DATA);
                if (!r.ok()) return fail("the server sent a message that is not valid");
                if (data.size() > ch.window) return fail("the server sent more than the channel allows");
                ch.window -= static_cast<uint32_t>(data.size());
                if (ch.close_sent) return;
                if (!ch.socket) {
                    // The session: read for the address, taken at once.
                    session_text_ += data;
                    if (session_text_.size() > MAX_SESSION_TEXT) {
                        session_text_.erase(0, session_text_.size() - MAX_SESSION_TEXT / 2);
                    }
                    if (const auto url = findPublicUrl(session_text_)) setUrl(*url);
                    consume(ch, static_cast<uint32_t>(data.size()));
                } else if (type == CHANNEL_DATA) {
                    ch.to_local += data;
                } else {
                    consume(ch, static_cast<uint32_t>(data.size()));
                }
                return;
            }
            case CHANNEL_EOF:
                ch.eof_in = true;
                return;
            case CHANNEL_CLOSE:
                ch.close_received = true;
                if (!ch.close_sent) {
                    send(Writer().byte(CHANNEL_CLOSE).u32(ch.peer).take());
                    ch.close_sent = true;
                    ch.closed_at  = Clock::now();
                }
                ch.socket.reset();
                return;
            case CHANNEL_REQUEST: {
                r.string(256);
                if (r.boolean()) send(Writer().byte(CHANNEL_FAILURE).u32(ch.peer).take());
                return;
            }
            default:
                return;
        }
    }

    SshTunnel::Shared& tunnel_;
    const TunnelSettings& cfg_;
    const std::atomic_bool& stopping_;
    End end_;

    std::unique_ptr<net::Socket> socket_;
    std::string in_, out_;
    Phase phase_ = Phase::Version;
    int lines_before_ = 0;
    Clock::time_point started_, heard_, last_keepalive_, keyed_at_;

    // The packets.
    PacketCipher rx_, tx_;
    uint32_t seq_rx_ = 0, seq_tx_ = 0;
    uint64_t bytes_ = 0;  // since the last keys

    // The key exchange.
    std::string v_c_, v_s_, i_c_, i_s_;
    std::string kex_, host_alg_, cipher_c_, cipher_s_, mac_c_, mac_s_;
    std::unique_ptr<X25519> ecdh_;
    std::array<std::string, 3> next_rx_;
    bool kexinit_sent_ = false;
    bool newkeys_sent_ = false;
    bool strict_       = false;
    bool initial_kex_done_ = false;  // the server's first NEWKEYS came
    std::string session_id_, host_key_;
    std::string known_print_;  // the host key trusted before, when the server shows another
    std::deque<std::string> held_;
    std::size_t held_bytes_ = 0;  // what held_ takes, about

    // The channels.
    std::deque<Reply> replies_;
    std::map<uint32_t, Channel> channels_;
    uint32_t next_id_  = 0;
    uint32_t session_  = 0;
    int relayed_count_ = 0;
    int bound_port_    = 0;
    std::string session_text_;
};

// ---------------------------------------------------------------------------------------------------------------

SshTunnel::~SshTunnel() { stop(); }

void SshTunnel::start(TunnelSettings settings) {
    stop();
    settings_                = std::move(settings);
    shared_                  = std::make_shared<Shared>();
    shared_->settings        = settings_;
    shared_->status.state    = TunnelState::Connecting;
    thread_                  = std::thread([shared = shared_] {
        // Nothing thrown on this thread may end the program: a failure, with its reason.
        try {
            run(shared);
        } catch (const std::exception& e) {
            failed(*shared, e.what());
        } catch (...) {
            failed(*shared, "unknown error");
        }
        shared->finished = true;
    });
}

void SshTunnel::stop() {
    if (!shared_) return;
    shared_->stopping = true;
    // A connection under way gives up at once, unless it is still finding the server's address, which nothing cuts
    // short: not waited for then. Once `stopping` is set, the thread touches nothing but `shared` (no Console, no
    // translation, no known hosts), which it owns with us: it may outlive the program's static objects at its end.
    const auto until = Clock::now() + std::chrono::milliseconds(1500);
    while (!shared_->finished && Clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (shared_->finished) {
        thread_.join();
    } else {
        thread_.detach();
    }
    shared_.reset();
}

void SshTunnel::failed(Shared& shared, const char* what) noexcept {
    try {
        shared.set(TunnelState::Failed, std::string("the tunnel stopped on an internal error: ") + what);
    } catch (...) {
    }
}

TunnelStatus SshTunnel::status() const {
    if (!shared_) return {};
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->status;
}

void SshTunnel::run(const std::shared_ptr<Shared>& shared) {
    const auto& settings = shared->settings;
    auto retry           = settings.first_retry;
    while (!shared->stopping) {
        Console::instance().add(trf("Tunnel: connecting to {}", settings.host));
        const auto start = Clock::now();
        TunnelConnection::End end;
        // What the server sends cannot end the program either (an exception escaping a thread would): the connection
        // fails, and is tried again.
        try {
            TunnelConnection connection(*shared, settings);
            end = connection.run();
        } catch (const std::exception& e) {
            end.reason = std::string("the connection ended on an internal error: ") + e.what();
        } catch (...) {
            end.reason = "the connection ended on an internal error";
        }
        if (shared->stopping) break;
        if (end.permanent) {
            Console::instance().add(trf("Tunnel: {}", end.reason), true);
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                shared->status.new_key = end.new_key;
            }
            shared->set(TunnelState::Failed, end.reason);
            break;
        }
        // Again in a moment, longer after each failure; from the start after a connection that lasted.
        if (end.was_on_air && Clock::now() - start > MAX_RETRY) retry = settings.first_retry;
        Console::instance().add(trf("Tunnel: {}; connecting again in {} s", end.reason, retry.count()), true);
        shared->set(TunnelState::Reconnecting, end.reason);
        const auto until = Clock::now() + retry;
        while (!shared->stopping && Clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        retry = std::min(retry * 2, std::chrono::duration_cast<std::chrono::seconds>(MAX_RETRY));
        if (!shared->stopping) shared->set(TunnelState::Connecting, end.reason);
    }
}

}  // namespace fcg
