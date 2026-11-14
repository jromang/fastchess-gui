#include "ssh_tunnel.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <vector>

#include <libssh2.h>
#include <mbedtls/platform_util.h>
#include <psa/crypto.h>

#include "console.hpp"
#include "i18n.hpp"
#include "net.hpp"
#include "storage.hpp"

namespace fcg {

namespace {

using Clock = std::chrono::steady_clock;

// localhost.run's RSA host key (ssh-keyscan -t rsa localhost.run | ssh-keygen -lf -). Its Ed25519 key cannot be
// checked with the cryptography at hand, so only RSA is asked for.
constexpr const char* LOCALHOST_RUN_KEY = "SHA256:FV8IMJ4IYjYUTnd6on7PqbRjaZf4c1EhhEBgeUdE94I";

// What is offered, best first: all the cryptography our libssh2 backend has (gui/libssh2-psa). libssh2 adds the
// strict key exchange against the Terrapin attack (kex-strict-c-v00@openssh.com) to the first list.
constexpr const char* KEX       = "curve25519-sha256,curve25519-sha256@libssh.org";
constexpr const char* HOST_KEYS = "rsa-sha2-512,rsa-sha2-256";
constexpr const char* CIPHERS   = "aes256-gcm@openssh.com,aes128-gcm@openssh.com,aes256-ctr,aes128-ctr";
constexpr const char* MACS = "hmac-sha2-256-etm@openssh.com,hmac-sha2-256,hmac-sha2-512-etm@openssh.com,hmac-sha2-512";

// The session's flow: what the server may send before it waits for us, and the largest data packet. A forwarded
// connection's flow is libssh2's (32 KiB packets, a window of 2 MiB that nothing lets a program change): it is read on
// every pass, RELAY_READ bytes at a time, whatever the web server takes, so that libssh2 holds for it only what came
// since the last pass, never a window of tiny packets (each of them a block of its memory, some 80 bytes for one).
constexpr unsigned SESSION_WINDOW   = 256 * 1024;
constexpr unsigned SESSION_MAX_DATA = 32768;
constexpr std::size_t RELAY_READ    = 16384;
// Connections relayed at once, those closed on our side that the server did not close yet included: the web server
// answers 32 at once and tells the next ones to come back. As many more wait their turn, read meanwhile; beyond,
// they are closed at once (or refused by libssh2, when more than QUEUE come at once).
constexpr std::size_t MAX_CHANNELS = 40;
constexpr std::size_t QUEUE        = MAX_CHANNELS;
// Channels closed on our side that libssh2 still waits on for the server's close, counted no more after a while:
// beyond this many, the connection ends.
constexpr std::size_t MAX_CLOSING = 4 * MAX_CHANNELS;
// What waits for the viewer, from the web server: beyond, the web server is not read meanwhile.
constexpr std::size_t RELAY_BUFFER = 64 * 1024;
// What the tunnel keeps for a viewer that the web server did not take, or that waits its turn: a viewer that sends
// more is let go, alone. A request to the web page is 8 KiB at most.
constexpr std::size_t VIEWER_BUDGET = 128 * 1024;
// What libssh2 may take from the server's socket in a pass, at most: however fast the server sends, what it holds for
// the forwarded connections, read on every pass, is then what came in two passes at most (512 KiB, about 1 MiB of
// memory in one-byte packets of 36 bytes), besides what comes for a connection closed on our side until the server
// closes it: what was on its way then.
constexpr std::size_t RECEIVE_PER_PASS = 256 * 1024;
// The tunnel's own memory for its viewers: what each connection relayed or waiting may keep (its budget, and one read
// more), and the web server's answers on their way. libssh2's is counted apart (TUNNEL_MEMORY).
static_assert((MAX_CHANNELS + QUEUE) * (VIEWER_BUDGET + RELAY_READ) + MAX_CHANNELS * RELAY_BUFFER <= TUNNEL_MEMORY / 2);
// libssh2's interval between keepalives, in seconds: 1 (which it takes as 2), for the one that could not go at once
// is sent on only after it. The tunnel's own interval, the server's silence, comes first.
constexpr unsigned KEEPALIVE_RETRY = 1;
constexpr auto KEEPALIVE_AGAIN     = std::chrono::seconds(2);
// The session's text kept to read the address in: its last 64 KiB at most (a new address, the domain rotated, comes
// at the end).
constexpr std::size_t MAX_SESSION_TEXT = 64 * 1024;
constexpr auto CONNECT_TIMEOUT         = std::chrono::seconds(15);
constexpr auto HANDSHAKE_TIMEOUT       = std::chrono::seconds(30);
constexpr auto MAX_RETRY               = std::chrono::seconds(60);
constexpr int LOCAL_CONNECT_TIMEOUT_MS = 200;
// How many times a pass over everything is repeated before waiting, as long as something moved.
constexpr int MAX_PASSES = 16;

constexpr uint32_t LOCALHOST = 0x7F000001;

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string hostPort(const std::string& host, int port) { return host + ":" + std::to_string(port); }

std::string base64(std::string_view data) {
    static constexpr char DIGITS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = static_cast<uint32_t>(static_cast<unsigned char>(data[i])) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(static_cast<unsigned char>(data[i + 1])) << 8;
        if (i + 2 < data.size()) n |= static_cast<unsigned char>(data[i + 2]);
        out += DIGITS[(n >> 18) & 63];
        out += DIGITS[(n >> 12) & 63];
        if (i + 1 < data.size()) out += DIGITS[(n >> 6) & 63];
        if (i + 2 < data.size()) out += DIGITS[n & 63];
    }
    return out;
}

// PSA and libssh2, once for the program: libssh2_exit() is never called, as PSA serves the rest of the program too.
bool startSsh() {
    static const bool ok = psa_crypto_init() == PSA_SUCCESS && libssh2_init(0) == 0;
    return ok;
}

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

std::optional<HostKeyRefusal> checkHostKey(const TunnelSettings& settings, const std::string& print) {
    const auto refused = [](std::string reason) { return HostKeyRefusal{std::move(reason), true, {}}; };
    // No fingerprint, as it could not be computed: nothing to compare, and nothing to keep (an empty one kept reads as
    // no key, trusted again the next time). Tried again later.
    if (print.empty()) {
        auto refusal =
            refused(trf("the host key of {} cannot be checked: its fingerprint could not be computed", settings.host));
        refusal.permanent = false;
        return refusal;
    }
    if (!settings.pinned.empty()) {
        if (print == settings.pinned) return std::nullopt;
        return refused(
            trf("The host key of {} is not the one expected ({}, instead of {}): someone may be in the "
                "way. Nothing was sent.",
                settings.host, print, settings.pinned));
    }
    // Nowhere to keep it: not trusted blindly, every time.
    if (settings.known_hosts.empty()) {
        return refused("the host key of " + settings.host + " cannot be checked: neither pinned nor kept");
    }
    const auto known = knownHostKey(settings.known_hosts, settings.host, settings.port);
    if (!known) {
        return refused(
            trf("{} cannot be read, so the host keys trusted before are unknown: nothing was sent to {}. "
                "Repair the file, or delete it to trust the server's key again.",
                settings.known_hosts.string(), settings.host));
    }
    if (known->empty()) {
        if (!rememberHostKey(settings.known_hosts, settings.host, settings.port, print)) {
            Console::instance().add(trf("Tunnel: the host key of {} ({}) could not be kept in {}", settings.host, print,
                                        settings.known_hosts.string()),
                                    true);
        } else {
            Console::instance().add(trf("Tunnel: the host key of {} is {}, trusted from now on", settings.host, print));
        }
        return std::nullopt;
    }
    if (*known == print) return std::nullopt;
    auto refusal =
        refused(trf("The host key of {} changed: {} now, {} before. Someone may be in the way; trust the "
                    "new key only if the server's owner says it changed.",
                    settings.host, print, *known));
    refusal.new_key = print;
    return refusal;
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

std::string fingerprint(std::string_view key_blob) {
    uint8_t hash[32];
    std::size_t n = 0;
    if (psa_crypto_init() != PSA_SUCCESS ||
        psa_hash_compute(PSA_ALG_SHA_256, reinterpret_cast<const uint8_t*>(key_blob.data()), key_blob.size(), hash,
                         sizeof(hash), &n) != PSA_SUCCESS) {
        return {};
    }
    return "SHA256:" + base64(std::string_view(reinterpret_cast<const char*>(hash), n));
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
// One connection to the server, from the version exchange to its end. libssh2 speaks SSH, without blocking, on our
// socket; this drives it, and relays what goes through the channels.

class TunnelConnection {
   public:
    TunnelConnection(SshTunnel::Shared& shared, const TunnelSettings& settings)
        : tunnel_(shared), cfg_(settings), stopping_(shared.stopping) {}
    ~TunnelConnection() { close(); }
    TunnelConnection(const TunnelConnection&)            = delete;
    TunnelConnection& operator=(const TunnelConnection&) = delete;

    struct End {
        std::string reason;  // empty when stopped
        bool permanent = false;
        std::string new_key;
        bool was_on_air = false;
    };

    End run() {
        if (!startSsh()) {
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
        if (!startSession()) return end_;

        while (!stopping_ && end_.reason.empty()) {
            wait();
            for (int pass = 0; pass < MAX_PASSES && !stopping_ && end_.reason.empty(); ++pass) {
                received_        = 0;
                const bool moved = advance();
                keepAlive();
                checkConnection();
                if (!moved) break;
            }
            timers();
        }
        if (stopping_ && end_.reason.empty()) goodbye();
        return end_;
    }

   private:
    // A viewer's connection: a channel the server forwarded, and the web server's connection it is relayed to, once
    // its turn came.
    struct Viewer {
        LIBSSH2_CHANNEL* channel = nullptr;
        std::unique_ptr<net::Socket> socket;
        bool queued = true;       // waiting its turn: no connection to the web server yet
        std::string to_local;     // for the web server
        std::string to_server;    // for the viewer
        bool server_eof = false;  // the server sends no more
        bool shut       = false;  // and the web server was told
        bool local_done = false;  // the web server sends no more
        // The last write to the channel could not go: the next one must start with the same bytes (libssh2 may have
        // a packet of them under way), and the channel cannot be closed before.
        bool write_blocked = false;
        // The last read could not send the window's adjustment it starts with: the next one is the same read (of
        // `read_room` bytes), and the channel cannot be closed before.
        bool read_pending     = false;
        std::size_t read_room = 0;
    };
    // A channel closed on our side, until libssh2 is done with it: the server closed it too, or something else came
    // first (libssh2 then forgets it: a late close would name a number it does not give again).
    struct Closing {
        LIBSSH2_CHANNEL* channel = nullptr;
        Clock::time_point since;
        bool relayed = false;  // it had a connection to the web server, and takes the place of one meanwhile
    };

    enum class Phase { Handshake, Auth, Forward, OnAir };
    enum class SessionChannel { Opening, Shell, Reading, Done };

    void fail(const std::string& reason, bool permanent = false) {
        if (!end_.reason.empty()) return;
        // Server text is in many reasons (DISCONNECT's, the authentication methods, libssh2's errors about what it
        // received): it goes to the window, the Console, and a headless run's terminal.
        end_.reason    = printableText(reason);
        end_.permanent = permanent;
    }

    // --- libssh2's callbacks: the session's memory, the socket, the server's DISCONNECT, libssh2's errors ---

    static TunnelConnection& of(void** abstract) { return *static_cast<TunnelConnection*>(*abstract); }

    // Everything libssh2 holds for the connection is counted, and bounded: the size is kept before each block. And
    // wiped when given back: keys, shared secrets and the clear text of packets pass through libssh2's memory.
    static constexpr std::size_t HEADER = alignof(std::max_align_t);

    static LIBSSH2_ALLOC_FUNC(allocate) { return of(abstract).take(nullptr, count); }
    static LIBSSH2_REALLOC_FUNC(reallocate) { return of(abstract).take(ptr, count); }
    static LIBSSH2_FREE_FUNC(release) { of(abstract).give(ptr); }

    void* take(void* old, std::size_t size) {
        const auto before = old ? *reinterpret_cast<std::size_t*>(static_cast<char*>(old) - HEADER) : 0;
        // Compared so that nothing wraps around, whatever the size asked for (`before` is counted in `held_`).
        if (size > cfg_.max_memory || held_ - before > cfg_.max_memory - size) {
            over_memory_ = true;
            return nullptr;
        }
        auto* block = static_cast<char*>(std::malloc(HEADER + size));
        if (!block) return nullptr;
        *reinterpret_cast<std::size_t*>(block) = size;
        held_ += size;
        if (old) {
            std::memcpy(block + HEADER, old, std::min(before, size));
            give(old);
        }
        return block + HEADER;
    }
    void give(void* ptr) {
        if (!ptr) return;
        auto* block     = static_cast<char*>(ptr) - HEADER;
        const auto size = *reinterpret_cast<std::size_t*>(block);
        held_ -= size;
        mbedtls_platform_zeroize(block, HEADER + size);
        std::free(block);
    }

    static LIBSSH2_RECV_FUNC(receive) {
        (void)socket;
        (void)flags;
        auto& self = of(abstract);
        if (self.received_ >= RECEIVE_PER_PASS) return -EAGAIN;
        const auto n =
            self.socket_->receive(static_cast<char*>(buffer), std::min(length, RECEIVE_PER_PASS - self.received_));
        if (n > 0) {
            self.heard_ = Clock::now();
            self.received_ += static_cast<std::size_t>(n);
            return n;
        }
        if (n == 0) return -EAGAIN;
        self.socket_closed_ = true;
        return 0;  // the end of the connection, to libssh2
    }
    static LIBSSH2_SEND_FUNC(transmit) {
        (void)socket;
        (void)flags;
        auto& self = of(abstract);
        if (self.cfg_.send_limit > 0) length = std::min(length, self.cfg_.send_limit);
        const auto n = self.socket_->send(std::string_view(static_cast<const char*>(buffer), length));
        if (n > 0) return n;
        if (n == 0) return -EAGAIN;
        self.socket_closed_ = true;
        return -EPIPE;
    }

    static LIBSSH2_DISCONNECT_FUNC(disconnected) {
        (void)session;
        (void)reason;
        (void)language;
        (void)language_len;
        auto& self = of(abstract);
        try {
            self.disconnect_reason_.assign(message ? message : "",
                                           message && message_len > 0 ? std::min<std::size_t>(message_len, 1024) : 0);
        } catch (...) {
        }
        self.disconnected_ = true;
    }

    // libssh2's errors as it meets them ("[libssh2] 0.000123 Failure Event: -11 - Unable to verify ..."): what fails
    // a call is often told only by the error before the last, the last one saying what was being done. During the
    // handshake, its packets too: whether the server's KEXINIT came, to tell algorithms not agreed on (nothing
    // else says so) from a server that sent no key exchange at all.
    static void traced(LIBSSH2_SESSION*, void* context, const char* data, std::size_t length) {
        auto& self = *static_cast<TunnelConnection*>(context);
        try {
            // What the line is comes right after libssh2's prefix ("[libssh2] 0.000123 "); what the server sent (its
            // version line, a DEBUG message) only after that, where it cannot pass for it.
            auto line                         = std::string_view(data, length);
            constexpr std::string_view PREFIX = "[libssh2] ";
            if (!line.starts_with(PREFIX)) return;
            line.remove_prefix(PREFIX.size());
            const auto digits = [&line] {
                std::size_t n = 0;
                while (n < line.size() && std::isdigit(static_cast<unsigned char>(line[n]))) ++n;
                line.remove_prefix(n);
                return n > 0;
            };
            if (!digits() || !line.starts_with('.')) return;
            line.remove_prefix(1);
            if (!digits() || !line.starts_with(' ')) return;
            line.remove_prefix(1);
            if (line.starts_with("Transport: Packet type 20 received")) self.kexinit_received_ = true;
            constexpr std::string_view FAILURE = "Failure Event: ";
            if (!line.starts_with(FAILURE)) return;
            const auto rest = line.substr(FAILURE.size());
            const auto dash = rest.find(" - ");
            if (dash == std::string_view::npos) return;
            const int code = std::atoi(std::string(rest.substr(0, dash)).c_str());
            // A channel's errors do not end the connection: they are not why it failed.
            if (self.first_error_ == 0 && code < 0 && connectionError(code)) {
                self.first_error_      = code;
                self.first_error_text_ = std::string(rest.substr(dash + 3, 256));
            }
        } catch (...) {
        }
    }

    // --- The session ---

    bool startSession() {
        session_ = libssh2_session_init_ex(&TunnelConnection::allocate, &TunnelConnection::release,
                                           &TunnelConnection::reallocate, this);
        if (!session_) {
            fail("the SSH session cannot start");
            return false;
        }
        libssh2_session_set_blocking(session_, 0);
        libssh2_session_callback_set2(session_, LIBSSH2_CALLBACK_RECV,
                                      reinterpret_cast<libssh2_cb_generic*>(&TunnelConnection::receive));
        libssh2_session_callback_set2(session_, LIBSSH2_CALLBACK_SEND,
                                      reinterpret_cast<libssh2_cb_generic*>(&TunnelConnection::transmit));
        libssh2_session_callback_set2(session_, LIBSSH2_CALLBACK_DISCONNECT,
                                      reinterpret_cast<libssh2_cb_generic*>(&TunnelConnection::disconnected));
        libssh2_trace_sethandler(session_, this, &TunnelConnection::traced);
        libssh2_trace(session_, LIBSSH2_TRACE_ERROR | LIBSSH2_TRACE_TRANS);
        const auto banner = std::string("SSH-2.0-fastchessgui_") + FCG_VERSION;
        if (libssh2_session_banner_set(session_, banner.c_str()) != 0 ||
            libssh2_session_method_pref(session_, LIBSSH2_METHOD_KEX, KEX) != 0 ||
            libssh2_session_method_pref(session_, LIBSSH2_METHOD_HOSTKEY, HOST_KEYS) != 0 ||
            libssh2_session_method_pref(session_, LIBSSH2_METHOD_CRYPT_CS, CIPHERS) != 0 ||
            libssh2_session_method_pref(session_, LIBSSH2_METHOD_CRYPT_SC, CIPHERS) != 0 ||
            libssh2_session_method_pref(session_, LIBSSH2_METHOD_MAC_CS, MACS) != 0 ||
            libssh2_session_method_pref(session_, LIBSSH2_METHOD_MAC_SC, MACS) != 0) {
            fail("the SSH session cannot be set up", true);
            return false;
        }
        return true;
    }

    std::string lastError() const {
        char* message = nullptr;
        int length    = 0;
        libssh2_session_last_error(session_, &message, &length, 0);
        return message ? std::string(message, static_cast<std::size_t>(std::max(length, 0))) : std::string();
    }

    // The connection failed in libssh2 (`rc`, what the call returned): why, as the user should read it. `instead`
    // replaces libssh2's last error, when it says nothing true of why.
    void failSsh(int rc, const std::string& instead = {}) {
        if (!end_.reason.empty()) return;
        if (over_memory_) return fail("the server sent more than the tunnel keeps in memory");
        if (disconnected_) return fail("the server ended the connection: " + disconnect_reason_);
        if (socket_closed_) {
            return fail(phase_ == Phase::OnAir ? "the server closed the connection"
                                               : "the server closed the connection before the tunnel opened");
        }
        const int code    = first_error_ ? first_error_ : rc;
        const auto detail = first_error_ ? first_error_text_ : !instead.empty() ? instead : lastError();
        switch (code) {
            case LIBSSH2_ERROR_HOSTKEY_SIGN:
                return fail(trf("{}: the host key's signature is not valid", cfg_.host), true);
            case LIBSSH2_ERROR_HOSTKEY_INIT:
                return fail(trf("{}: the host key cannot be used (RSA keys of 2048 to 4096 bits only)", cfg_.host),
                            true);
            case LIBSSH2_ERROR_KEX_FAILURE:
                // Algorithms not agreed on have no error of their own: this one, and none before, after the server's
                // KEXINIT. Without it, what the server sent was not a packet libssh2 could take.
                if (detail == "Unable to exchange encryption keys") {
                    if (!kexinit_received_) return fail("the server sent a packet that is not valid (" + detail + ")");
                    return fail(trf("{} has no algorithm in common with fastchess-gui (it needs curve25519-sha256, an "
                                    "RSA host key and AES)",
                                    cfg_.host),
                                true);
                }
                break;
            case LIBSSH2_ERROR_INVALID_MAC:
            case LIBSSH2_ERROR_DECRYPT:
            case LIBSSH2_ERROR_PROTO:
            case LIBSSH2_ERROR_BUFFER_TOO_SMALL:
            case LIBSSH2_ERROR_OUT_OF_BOUNDARY:
                return fail("the server sent a packet that is not valid (" + detail + ")");
            default:
                break;
        }
        fail("the SSH connection failed (" + detail + ")");
    }

    // A failure of the whole connection, rather than of one channel's.
    static bool connectionError(int rc) {
        switch (rc) {
            case LIBSSH2_ERROR_EAGAIN:
            case LIBSSH2_ERROR_CHANNEL_OUTOFORDER:
            case LIBSSH2_ERROR_CHANNEL_FAILURE:
            case LIBSSH2_ERROR_CHANNEL_REQUEST_DENIED:
            case LIBSSH2_ERROR_CHANNEL_UNKNOWN:
            case LIBSSH2_ERROR_CHANNEL_WINDOW_EXCEEDED:
            case LIBSSH2_ERROR_CHANNEL_PACKET_EXCEEDED:
            case LIBSSH2_ERROR_CHANNEL_CLOSED:
            case LIBSSH2_ERROR_CHANNEL_EOF_SENT:
            case LIBSSH2_ERROR_CHANNEL_WINDOW_FULL:
            case LIBSSH2_ERROR_BAD_USE:
            case LIBSSH2_ERROR_INVAL:
                return false;
            default:
                return true;
        }
    }

    // What libssh2 could not say through a call's result: the connection gone, or out of memory.
    void checkConnection() {
        if (over_memory_ || disconnected_ || socket_closed_) failSsh(LIBSSH2_ERROR_SOCKET_DISCONNECT);
    }

    // The sockets: until the server's socket or a web server's connection can go on, or 100 ms (none when there is
    // something libssh2 already received and holds for a channel, read on the next pass). A read waiting to send its
    // window's adjustment waits for the server's socket, as a write does; a keepalive under way, for libssh2 to send
    // it on.
    void wait() {
        std::vector<net::Wait> waits;
        int timeout    = 100;
        bool outbound  = outboundBlocked();
        const auto now = Clock::now();
        if (keepalive_pending_ && now < keepalive_again_) {
            outbound = false;
            timeout  = static_cast<int>(std::min<int64_t>(
                timeout, std::chrono::duration_cast<std::chrono::milliseconds>(keepalive_again_ - now).count() + 1));
        }
        waits.push_back({socket_->handle(), true, outbound});
        bool ready = false;
        for (const auto& v : viewers_) {
            ready = ready || (!v->read_pending && !v->server_eof && waiting(v->channel) > 0) ||
                    (!v->to_server.empty() && !v->write_blocked && libssh2_channel_window_write(v->channel) > 0);
            if (!v->socket) continue;
            waits.push_back(
                {v->socket->handle(), !v->local_done && v->to_server.size() < RELAY_BUFFER, !v->to_local.empty()});
        }
        ready = ready || ((session_state_ == SessionChannel::Shell || session_state_ == SessionChannel::Reading) &&
                          !session_read_pending_ && waiting(session_channel_) > 0);
        net::waitAny(waits, ready ? 0 : timeout);
    }

    // What libssh2 received for `channel` and holds until it is read.
    static std::size_t waiting(LIBSSH2_CHANNEL* channel) {
        unsigned long available = 0;
        libssh2_channel_window_read_ex(channel, &available, nullptr);
        return available;
    }

    // A packet libssh2 started to send is under way, the socket full: it sends nothing else until the call that
    // started it is made again.
    bool outboundBlocked() const {
        return (libssh2_session_block_directions(session_) & LIBSSH2_SESSION_BLOCK_OUTBOUND) != 0;
    }

    // One pass over everything; true when something moved.
    bool advance() {
        switch (phase_) {
            case Phase::Handshake:
                return handshake();
            case Phase::Auth:
                return authenticate();
            case Phase::Forward:
                return forward();
            case Phase::OnAir:
                return serve();
        }
        return false;
    }

    void timers() {
        if (!end_.reason.empty()) return;
        const auto now = Clock::now();
        if (phase_ != Phase::OnAir) {
            if (now - started_ > HANDSHAKE_TIMEOUT) fail("no answer from the server in time");
            return;
        }
        if (now - heard_ > cfg_.keepalive * 3 + std::chrono::seconds(5)) return fail("the server stopped answering");
    }

    // A keepalive, only when the server said nothing for a while: its answer is then all that comes, and libssh2
    // keeps the answers (a few bytes each). One that did not go whole holds back everything else: libssh2 sends the
    // rest from the same call only, made again once its interval passed, so it is made on every pass until then,
    // whatever the server says meanwhile. From here only: the packet's address must be the same each time.
    void keepAlive() {
        if (phase_ != Phase::OnAir || !end_.reason.empty()) return;
        if (!keepalive_pending_ && Clock::now() - heard_ < cfg_.keepalive) return;
        int next     = 0;
        const int rc = libssh2_keepalive_send(session_, &next);
        if (rc != 0 && rc != LIBSSH2_ERROR_EAGAIN) return failSsh(rc);
        // Sent, or tried, in this call: the next try is a whole interval away.
        if (next >= static_cast<int>(KEEPALIVE_AGAIN.count())) keepalive_again_ = Clock::now() + KEEPALIVE_AGAIN;
        keepalive_pending_ = outboundBlocked();
    }

    void goodbye() {
        if (!session_ || phase_ == Phase::Handshake || socket_closed_) return;
        auto until = Clock::now() + std::chrono::seconds(1);
        int rc     = LIBSSH2_ERROR_EAGAIN;
        while (Clock::now() < until) {
            rc = libssh2_session_disconnect(session_, "bye");
            if (rc != LIBSSH2_ERROR_EAGAIN) break;
            std::vector<net::Wait> waits{{socket_->handle(), true, true}};
            net::waitAny(waits, 100);
        }
        if (rc != 0) return;
        // A socket closed with data it has not read resets the connection, and on Windows the server then loses what
        // it had not read yet, the DISCONNECT included. So the end of the stream goes after it, and what the server
        // still sends is read until it closes too, a second at most.
        socket_->shutdownWrite();
        until = Clock::now() + std::chrono::seconds(1);
        char discard[4096];
        while (Clock::now() < until) {
            const long n = socket_->receive(discard, sizeof discard);
            if (n < 0) break;
            if (n > 0) continue;
            std::vector<net::Wait> waits{{socket_->handle(), true, false}};
            net::waitAny(waits, 100);
        }
    }

    // The session and all it holds, its channels included, then the socket. What libssh2 would still send or wait
    // for fails at once: the socket is shut first.
    void close() {
        for (auto& v : viewers_) v->socket.reset();
        if (session_) {
            if (socket_) socket_->shutdownBoth();
            for (int i = 0; i < 100 && libssh2_session_free(session_) == LIBSSH2_ERROR_EAGAIN; ++i) {
            }
            session_ = nullptr;
        }
        socket_.reset();
    }

    // --- Before the tunnel ---

    bool handshake() {
        const int rc = libssh2_session_handshake(session_, socket_->handle());
        if (rc == LIBSSH2_ERROR_EAGAIN) return false;
        if (rc != 0) {
            failSsh(rc);
            return false;
        }
        // Its signature of the exchange was checked by libssh2: the key can be compared, and trusted.
        std::size_t length = 0;
        int type           = 0;
        const char* key    = libssh2_session_hostkey(session_, &length, &type);
        if (!key || type != LIBSSH2_HOSTKEY_TYPE_RSA) {
            fail(trf("{} did not show an RSA host key", cfg_.host), true);
            return false;
        }
        if (!checkHostKey(std::string(key, length))) return false;
        first_error_ = 0;
        libssh2_trace(session_, LIBSSH2_TRACE_ERROR);
        phase_ = Phase::Auth;
        return true;
    }

    // The host key against the one pinned, or the one trusted the first time, which is kept then. False when the
    // connection failed.
    bool checkHostKey(const std::string& blob) {
        const auto refusal = fcg::checkHostKey(cfg_, fingerprint(blob));
        if (!refusal) return true;
        end_.new_key = refusal->new_key;
        fail(refusal->reason, refusal->permanent);
        return false;
    }

    // The "none" method: the services let anyone in, as long as the user is the one they expect.
    bool authenticate() {
        const char* methods =
            libssh2_userauth_list(session_, cfg_.user.c_str(), static_cast<unsigned>(cfg_.user.size()));
        if (!methods) {
            if (libssh2_userauth_authenticated(session_)) {
                phase_ = Phase::Forward;
                return true;
            }
            const int rc = libssh2_session_last_errno(session_);
            if (rc == LIBSSH2_ERROR_EAGAIN) return false;
            failSsh(rc);
            return false;
        }
        fail(trf("{} does not let user \"{}\" in without a key or a password (it accepts: {})", cfg_.host, cfg_.user,
                 std::string(methods).substr(0, 1024)),
             true);
        return false;
    }

    bool forward() {
        int bound = 0;
        listener_ =
            libssh2_channel_forward_listen_ex(session_, "localhost", cfg_.remote_port, &bound, static_cast<int>(QUEUE));
        if (!listener_) {
            const int rc = libssh2_session_last_errno(session_);
            if (rc == LIBSSH2_ERROR_EAGAIN) return false;
            if (connectionError(rc) && rc != LIBSSH2_ERROR_REQUEST_DENIED) {
                failSsh(rc);
            } else {
                fail(trf("{} refused to forward its port {}", cfg_.host, cfg_.remote_port));
            }
            return false;
        }
        bound_port_     = cfg_.remote_port != 0 ? cfg_.remote_port : bound;
        phase_          = Phase::OnAir;
        end_.was_on_air = true;
        {
            std::lock_guard<std::mutex> lock(tunnel_.mutex);
            tunnel_.status.state = TunnelState::OnAir;
            tunnel_.status.error.clear();
        }
        libssh2_keepalive_config(session_, 1, KEEPALIVE_RETRY);
        // Until the services print the address on the session, or for a server of one's own that prints nothing:
        // the server and the port.
        if (cfg_.pinned.empty()) {
            setUrl(bound_port_ == 80 ? "http://" + cfg_.host
                                     : "http://" + cfg_.host + ":" + std::to_string(bound_port_));
        }
        return true;
    }

    void setUrl(const std::string& url) {
        std::lock_guard<std::mutex> lock(tunnel_.mutex);
        if (tunnel_.status.url == url) return;
        tunnel_.status.url = url;
        Console::instance().add(trf("Tunnel: the web page is on {}", url));
    }

    // --- On the air ---

    bool serve() {
        // The closing channels first: their waiting reads what came for the others too, which are served next.
        bool moved = closeChannels();
        moved      = serveSession() || moved;
        moved      = accept() || moved;
        for (std::size_t i = 0; i < viewers_.size() && end_.reason.empty();) {
            moved = relay(*viewers_[i]) || moved;
            if (!viewers_[i]->channel) {
                viewers_.erase(viewers_.begin() + static_cast<std::ptrdiff_t>(i));
                moved = true;
            } else {
                ++i;
            }
        }
        const int count =
            static_cast<int>(std::count_if(viewers_.begin(), viewers_.end(), [](const auto& v) { return !v->queued; }));
        if (count != relayed_count_) {
            relayed_count_ = count;
            std::lock_guard<std::mutex> lock(tunnel_.mutex);
            tunnel_.status.connections = count;
        }
        return moved && end_.reason.empty();
    }

    // The services print the address on a session: one, with a shell, as `ssh -R` would open. Without it, the
    // tunnel works all the same.
    bool serveSession() {
        switch (session_state_) {
            case SessionChannel::Opening: {
                session_channel_ =
                    libssh2_channel_open_ex(session_, "session", 7, SESSION_WINDOW, SESSION_MAX_DATA, nullptr, 0);
                if (!session_channel_) {
                    const int rc = libssh2_session_last_errno(session_);
                    if (rc == LIBSSH2_ERROR_EAGAIN) return false;
                    if (connectionError(rc)) failSsh(rc);
                    session_state_ = SessionChannel::Done;
                    return true;
                }
                libssh2_channel_handle_extended_data2(session_channel_, LIBSSH2_CHANNEL_EXTENDED_DATA_MERGE);
                session_state_ = SessionChannel::Shell;
                return true;
            }
            case SessionChannel::Shell: {
                // localhost.run never answers the request, and prints all the same: what comes is read meanwhile.
                const int rc = libssh2_channel_shell(session_channel_);
                if (rc == LIBSSH2_ERROR_EAGAIN) return readSession();
                // Refused, or no answer in time (libssh2's 60 seconds): the session is read all the same. An error of
                // the connection's is told by the socket, or the next call.
                if (rc != 0 && first_error_ == rc) first_error_ = 0;
                session_state_ = SessionChannel::Reading;
                readSession();
                return true;
            }
            case SessionChannel::Reading:
                return readSession();
            case SessionChannel::Done:
                return false;
        }
        return false;
    }

    // What the server prints on the session: the public address is in it.
    bool readSession() {
        bool moved = false;
        char buffer[16384];
        while (true) {
            // Called on every pass: a read that could not send its window's adjustment is made again.
            const auto n          = libssh2_channel_read(session_channel_, buffer, sizeof(buffer));
            session_read_pending_ = n == LIBSSH2_ERROR_EAGAIN && outboundBlocked();
            if (n > 0) {
                moved = true;
                session_text_.append(buffer, static_cast<std::size_t>(n));
                if (session_text_.size() > MAX_SESSION_TEXT) {
                    session_text_.erase(0, session_text_.size() - MAX_SESSION_TEXT / 2);
                }
                continue;
            }
            if (n == LIBSSH2_ERROR_EAGAIN) break;
            if (n < 0 && connectionError(static_cast<int>(n))) {
                failSsh(static_cast<int>(n));
                return false;
            }
            if (n < 0 || libssh2_channel_eof(session_channel_) == 1) {
                startClosing(session_channel_, false);
                session_channel_ = nullptr;
                session_state_   = SessionChannel::Done;
                return true;
            }
            break;
        }
        if (moved) {
            if (const auto url = findPublicUrl(session_text_)) setUrl(*url);
        }
        return moved;
    }

    // The connections the server forwarded: all of them taken from libssh2 on every pass, to be read. Each waits its
    // turn, and is relayed to the web server on this machine as soon as there is a place; beyond QUEUE waiting,
    // closed at once.
    bool accept() {
        bool moved = turns();
        while (end_.reason.empty()) {
            LIBSSH2_CHANNEL* channel = libssh2_channel_forward_accept(listener_);
            if (!channel) {
                // Anything but "nothing yet" is the connection's failure: libssh2 tells them all here as a channel not
                // found, which says nothing of why.
                const int rc = libssh2_session_last_errno(session_);
                if (rc != LIBSSH2_ERROR_EAGAIN) {
                    failSsh(rc, rc == LIBSSH2_ERROR_CHANNEL_UNKNOWN ? "what the server sent could not be read"
                                                                    : std::string());
                }
                break;
            }
            moved = true;
            libssh2_channel_handle_extended_data2(channel, LIBSSH2_CHANNEL_EXTENDED_DATA_IGNORE);
            const auto queued =
                std::count_if(viewers_.begin(), viewers_.end(), [](const auto& v) { return v->queued; });
            if (static_cast<std::size_t>(queued) >= QUEUE) {
                startClosing(channel, false);
                continue;
            }
            auto viewer     = std::make_unique<Viewer>();
            viewer->channel = channel;
            viewers_.push_back(std::move(viewer));
            turns();
        }
        return moved;
    }

    // The connections waiting their turn, first come first served: relayed while there are places.
    bool turns() {
        bool moved      = false;
        const auto live = liveChannels();
        auto places     = live < MAX_CHANNELS ? MAX_CHANNELS - live : 0;
        for (auto& v : viewers_) {
            if (places == 0) break;
            if (!v->queued || v->local_done) continue;
            // Every channel waits meanwhile: a web server that does not answer at once is not there.
            const auto fd = net::connectTo(LOCALHOST, static_cast<uint16_t>(cfg_.local_port), LOCAL_CONNECT_TIMEOUT_MS);
            v->queued     = false;
            moved         = true;
            --places;
            if (fd) {
                v->socket = std::make_unique<net::Socket>(*fd);
            } else {
                v->to_local.clear();
                v->local_done = true;
            }
        }
        return moved;
    }

    // The channels relayed at the moment, and those closed on our side that the server did not close yet, for a
    // while: a server that never closes them must not have more and more.
    std::size_t liveChannels() const {
        const auto now    = Clock::now();
        std::size_t count = 0;
        for (const auto& v : viewers_) count += v->queued ? 0 : 1;
        for (const auto& c : closing_) count += c.relayed && now - c.since < cfg_.close_timeout ? 1 : 0;
        return count;
    }

    bool relay(Viewer& v) {
        bool moved = false;
        // From the viewer to the web server, read whether the web server takes it or not, a connection waiting its
        // turn included: what libssh2 holds for it would otherwise be up to a window of packets. A read starts by
        // adjusting the channel's window, when it runs low: when that could not go whole, libssh2 sends nothing else
        // until the same read is made again, so it is, before anything else is made of the channel. The end of what
        // the viewer sends: a read that gives nothing. Read until libssh2 holds nothing more for it: as it takes so
        // much from the socket in a pass, this ends.
        char buffer[RELAY_READ];
        while (v.read_pending || !v.server_eof) {
            if (!v.read_pending) v.read_room = sizeof(buffer);
            const auto n   = libssh2_channel_read(v.channel, buffer, v.read_room);
            v.read_pending = n == LIBSSH2_ERROR_EAGAIN && outboundBlocked();
            if (n == LIBSSH2_ERROR_EAGAIN) break;
            moved = true;
            if (n < 0 && connectionError(static_cast<int>(n))) {
                failSsh(static_cast<int>(n));
                return false;
            }
            if (n <= 0) {
                v.server_eof = true;
                break;
            }
            // Let go, or the web server gone: what comes is dropped.
            if (!v.local_done || v.socket) {
                v.to_local.append(buffer, static_cast<std::size_t>(n));
                sendLocal(v);
                if (v.to_local.size() > VIEWER_BUDGET) {
                    // More than the web server takes, or than a viewer waiting its turn may send: let go, alone.
                    v.socket.reset();
                    v.to_local.clear();
                    if (!v.write_blocked) v.to_server.clear();
                    v.local_done = true;
                }
            }
            // Less than asked: libssh2 holds nothing more for it.
            if (static_cast<std::size_t>(n) < v.read_room) break;
        }
        moved = sendLocal(v) || moved;
        if (v.socket && v.server_eof && v.to_local.empty() && !v.shut) {
            v.socket->shutdownWrite();
            v.shut = true;
        }
        // The server closed the channel (not only its side of it): the viewer is gone, the answer goes nowhere.
        if (v.server_eof && !v.local_done && libssh2_channel_wait_closed(v.channel) == 0) {
            v.socket.reset();
            v.to_local.clear();
            if (!v.write_blocked) v.to_server.clear();
            v.local_done = true;
            moved        = true;
        }

        // From the web server to the viewer.
        if (v.socket && !v.local_done && v.to_server.size() < RELAY_BUFFER) {
            const auto n = v.socket->receive(v.to_server, RELAY_BUFFER - v.to_server.size());
            if (n < 0) {
                v.local_done = true;  // it answered, and closed
                moved        = true;
            } else if (n > 0) {
                moved = true;
            }
        }
        if (!v.to_server.empty()) {
            const auto n = libssh2_channel_write(v.channel, v.to_server.data(), v.to_server.size());
            // With no window left, nothing was under way.
            v.write_blocked = n == LIBSSH2_ERROR_EAGAIN && libssh2_channel_window_write(v.channel) > 0;
            if (n > 0) {
                v.to_server.erase(0, static_cast<std::size_t>(n));
                moved = true;
            } else if (n < 0 && n != LIBSSH2_ERROR_EAGAIN) {
                if (connectionError(static_cast<int>(n))) {
                    failSsh(static_cast<int>(n));
                    return false;
                }
                v.to_server.clear();
                v.local_done = true;
            }
        }
        // Done both ways, or the web server gone: the channel is closed (EOF, then CLOSE), unless a write or a read of
        // it is under way.
        if (v.local_done && v.to_server.empty() && !v.write_blocked && !v.read_pending) {
            v.socket.reset();
            startClosing(v.channel, !v.queued);
            v.channel = nullptr;
            moved     = true;
        }
        return moved;
    }

    // What waits for the web server, as far as it takes it; true when it took something.
    static bool sendLocal(Viewer& v) {
        if (!v.socket || v.to_local.empty()) return false;
        const auto n = v.socket->send(v.to_local);
        if (n < 0) {
            v.socket.reset();  // the web server went: what it did not take is dropped
            v.to_local.clear();
            v.local_done = true;
            return true;
        }
        v.to_local.erase(0, static_cast<std::size_t>(n));
        return n > 0;
    }

    void startClosing(LIBSSH2_CHANNEL* channel, bool relayed) { closing_.push_back({channel, Clock::now(), relayed}); }

    // The channels closed on our side: let go of once libssh2 is done with them. Too many it still waits on, and
    // the connection ends.
    bool closeChannels() {
        bool moved = false;
        for (auto it = closing_.begin(); it != closing_.end() && end_.reason.empty();) {
            const int rc = libssh2_channel_close(it->channel);
            if (rc == LIBSSH2_ERROR_EAGAIN) {
                ++it;
                continue;
            }
            if (rc != 0 && connectionError(rc)) {
                failSsh(rc);
                return false;
            }
            libssh2_channel_free(it->channel);
            it    = closing_.erase(it);
            moved = true;
        }
        // Those that never had a place are counted once the server is late only: a viewer's connections closed at
        // once, however many, end nothing.
        const auto now        = Clock::now();
        const auto unanswered = std::count_if(closing_.begin(), closing_.end(), [&](const Closing& c) {
            return c.relayed || now - c.since >= cfg_.close_timeout;
        });
        if (static_cast<std::size_t>(unanswered) > MAX_CLOSING) {
            fail("the server does not close the connections it forwarded");
        }
        return moved;
    }

    SshTunnel::Shared& tunnel_;
    const TunnelSettings& cfg_;
    const std::atomic_bool& stopping_;
    End end_;

    std::unique_ptr<net::Socket> socket_;
    LIBSSH2_SESSION* session_   = nullptr;
    LIBSSH2_LISTENER* listener_ = nullptr;
    Phase phase_                = Phase::Handshake;
    Clock::time_point started_, heard_;

    // What libssh2 told, or holds.
    std::size_t held_     = 0;
    bool over_memory_     = false;
    std::size_t received_ = 0;  // from the socket, in this pass
    bool socket_closed_   = false;
    bool disconnected_    = false;
    std::string disconnect_reason_;
    int first_error_ = 0;
    std::string first_error_text_;
    bool kexinit_received_ = false;
    // A keepalive under way (the socket full), sent on when libssh2's interval passed, at `keepalive_again_`.
    bool keepalive_pending_ = false;
    Clock::time_point keepalive_again_;
    bool session_read_pending_ = false;  // as a viewer's read_pending, for the session channel

    // The channels.
    LIBSSH2_CHANNEL* session_channel_ = nullptr;
    SessionChannel session_state_     = SessionChannel::Opening;
    std::string session_text_;
    std::vector<std::unique_ptr<Viewer>> viewers_;
    std::vector<Closing> closing_;
    int relayed_count_ = 0;
    int bound_port_    = 0;
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
