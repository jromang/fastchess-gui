#include "ssh_client.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>

#include <libssh2.h>  // before windows.h: it includes winsock2.h
#include <libssh2_sftp.h>
#ifndef _WIN32
#    include <sys/socket.h>
#endif
#include <mbedtls/platform_util.h>
#include <psa/crypto.h>

#include "i18n.hpp"
#include "net.hpp"
#include "ssh_tunnel.hpp"

namespace fcg {

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

// What is offered, as the tunnel does: all the cryptography our libssh2 backend has (gui/libssh2-psa). The servers
// of Linux distributions, macOS and Windows have them all, an RSA host key included.
constexpr const char* KEX       = "curve25519-sha256,curve25519-sha256@libssh.org";
constexpr const char* HOST_KEYS = "rsa-sha2-512,rsa-sha2-256";
constexpr const char* CIPHERS   = "aes256-gcm@openssh.com,aes128-gcm@openssh.com,aes256-ctr,aes128-ctr";
constexpr const char* MACS = "hmac-sha2-256-etm@openssh.com,hmac-sha2-256,hmac-sha2-512-etm@openssh.com,hmac-sha2-512";

// A file's bytes go in pieces this large: libssh2 sends one SFTP packet after the other without waiting for each
// answer, as long as the piece lasts.
constexpr std::size_t TRANSFER_PIECE = std::size_t(1) << 20;
// What waiting for the socket takes at most before the cancellation and the timeout are looked at again.
constexpr int WAIT_STEP_MS = 100;

bool startSsh() {
    static const bool ok = psa_crypto_init() == PSA_SUCCESS && libssh2_init(0) == 0;
    return ok;
}

std::string decodeBase64(std::string_view text) {
    std::string out;
    uint32_t acc = 0;
    int bits     = 0;
    for (const char c : text) {
        int v = -1;
        if (c >= 'A' && c <= 'Z')
            v = c - 'A';
        else if (c >= 'a' && c <= 'z')
            v = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            v = c - '0' + 52;
        else if (c == '+')
            v = 62;
        else if (c == '/')
            v = 63;
        else
            continue;  // line breaks, padding
        acc = acc << 6 | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    return out;
}

// An SSH string (a 32-bit length and its bytes) at `at` of `data`, moved past; nullopt when it does not fit.
std::optional<std::string_view> sshString(std::string_view data, std::size_t& at) {
    if (at + 4 > data.size()) return std::nullopt;
    const auto* p    = reinterpret_cast<const unsigned char*>(data.data() + at);
    const uint32_t n = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    at += 4;
    if (n > data.size() - at) return std::nullopt;
    const auto s = data.substr(at, n);
    at += n;
    return s;
}

std::optional<std::string> readFile(const fs::path& file, std::size_t max) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::string data;
    char buffer[4096];
    while (in.read(buffer, sizeof buffer) || in.gcount() > 0) {
        data.append(buffer, static_cast<std::size_t>(in.gcount()));
        if (data.size() > max) return std::nullopt;
    }
    return data;
}

}  // namespace

KeyFileInfo inspectKeyFile(const fs::path& file) {
    KeyFileInfo info;
    auto data = readFile(file, 1 << 20);
    if (!data) {
        info.error = trf("The key file {} cannot be read.", file.string());
        return info;
    }
    info.readable                      = true;
    constexpr std::string_view OPENSSH = "-----BEGIN OPENSSH PRIVATE KEY-----";
    constexpr std::string_view END     = "-----END OPENSSH PRIVATE KEY-----";
    if (const auto begin = data->find(OPENSSH); begin != std::string::npos) {
        const auto end = data->find(END, begin);
        // openssh-key-v1\0, the cipher, the key derivation, its options, the number of keys, the first public key.
        const auto raw                   = decodeBase64(std::string_view(*data).substr(
            begin + OPENSSH.size(), end == std::string::npos ? std::string::npos : end - begin - OPENSSH.size()));
        constexpr std::string_view MAGIC = std::string_view("openssh-key-v1\0", 15);
        std::size_t at                   = MAGIC.size();
        const auto cipher                = raw.starts_with(MAGIC) ? sshString(raw, at) : std::nullopt;
        const auto kdf                   = cipher ? sshString(raw, at) : std::nullopt;
        const auto options               = kdf ? sshString(raw, at) : std::nullopt;
        at += 4;  // the number of keys
        const auto pub = options ? sshString(raw, at) : std::nullopt;
        if (!pub) {
            info.error = trf("{} is not a valid private key file.", file.string());
        } else {
            info.encrypted  = *cipher != "none";
            std::size_t pat = 0;
            if (const auto type = sshString(*pub, pat)) info.type = std::string(*type);
        }
    } else if (data->find("-----BEGIN RSA PRIVATE KEY-----") != std::string::npos) {
        info.type      = "ssh-rsa";
        info.encrypted = data->find("Proc-Type: 4,ENCRYPTED") != std::string::npos;
        if (info.encrypted) {
            info.error = trf("{} is in an old encrypted format: convert it with ssh-keygen -p -f \"{}\".",
                             file.string(), file.string());
        }
    } else if (data->find("PRIVATE KEY-----") != std::string::npos) {
        info.error = trf("{} is in a format fastchess-gui cannot read: convert it with ssh-keygen -p -f \"{}\".",
                         file.string(), file.string());
    } else {
        info.error = trf("{} is not a private key file (the public key ends in .pub; choose the file without it).",
                         file.string());
    }
    if (info.error.empty() && !info.type.empty() && info.type != "ssh-rsa" && info.type != "ssh-ed25519") {
        info.error =
            trf("{} holds a key of type {}: fastchess-gui logs in with RSA and Ed25519 keys only (make one "
                "with ssh-keygen -t ed25519), or with the SSH agent.",
                file.string(), info.type);
    }
    mbedtls_platform_zeroize(data->data(), data->size());
    return info;
}

std::string sftpPath(const std::string& path) {
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
        std::string out = "/" + path;
        std::replace(out.begin(), out.end(), '\\', '/');
        return out;
    }
    return path;
}

std::string remoteParent(const std::string& path) {
    const auto at = path.find_last_of("/\\");
    if (at == std::string::npos) return {};
    return at == 0 ? path.substr(0, 1) : path.substr(0, at);
}

std::string remoteName(const std::string& path) {
    const auto at = path.find_last_of("/\\");
    return at == std::string::npos ? path : path.substr(at + 1);
}

std::string remoteJoin(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    if (dir.back() == '/' || dir.back() == '\\') return dir + name;
    return dir + "/" + name;
}

// ---------------------------------------------------------------------------------------------------------------

struct SshClient::Impl {
    SshClient& self;
    SshTarget target;
    std::unique_ptr<net::Socket> socket;
    LIBSSH2_SESSION* session = nullptr;
    LIBSSH2_SFTP* sftp       = nullptr;
    bool socket_closed       = false;

    explicit Impl(SshClient& client) : self(client) {}
    ~Impl() { close(); }

    // --- libssh2's callbacks: the session's memory (wiped when given back: keys pass through it), the socket ---

    static constexpr std::size_t HEADER = alignof(std::max_align_t);

    static LIBSSH2_ALLOC_FUNC(allocate) {
        (void)abstract;
        return take(nullptr, count);
    }
    static LIBSSH2_REALLOC_FUNC(reallocate) {
        (void)abstract;
        return take(ptr, count);
    }
    static LIBSSH2_FREE_FUNC(release) {
        (void)abstract;
        give(ptr);
    }
    static void* take(void* old, std::size_t size) {
        auto* block = static_cast<char*>(std::malloc(HEADER + size));
        if (!block) return nullptr;
        *reinterpret_cast<std::size_t*>(block) = size;
        if (old) {
            const auto before = *reinterpret_cast<std::size_t*>(static_cast<char*>(old) - HEADER);
            std::memcpy(block + HEADER, old, std::min(before, size));
            give(old);
        }
        return block + HEADER;
    }
    static void give(void* ptr) {
        if (!ptr) return;
        auto* block = static_cast<char*>(ptr) - HEADER;
        mbedtls_platform_zeroize(block, HEADER + *reinterpret_cast<std::size_t*>(block));
        std::free(block);
    }

    static Impl& of(void** abstract) { return *static_cast<Impl*>(*abstract); }

    // libssh2 talks to the SSH agent through these too, on the agent's socket (a Unix socket, which blocks): sent and
    // received as they are.
    static ssize_t agentIo(libssh2_socket_t socket, void* buffer, std::size_t length, bool send) {
#ifdef _WIN32
        const int n = send ? ::send(socket, static_cast<const char*>(buffer), static_cast<int>(length), 0)
                           : ::recv(socket, static_cast<char*>(buffer), static_cast<int>(length), 0);
        return n >= 0 ? n : -EIO;
#else
#    ifdef MSG_NOSIGNAL
        constexpr int SEND_FLAGS = MSG_NOSIGNAL;
#    else
        constexpr int SEND_FLAGS = 0;  // SIGPIPE is ignored by the program (main.cpp)
#    endif
        const auto n = send ? ::send(socket, buffer, length, SEND_FLAGS) : ::recv(socket, buffer, length, 0);
        return n >= 0 ? n : -errno;
#endif
    }

    static LIBSSH2_RECV_FUNC(receive) {
        (void)flags;
        auto& self = of(abstract);
        if (!self.socket || socket != static_cast<libssh2_socket_t>(self.socket->handle())) {
            return agentIo(socket, buffer, length, false);
        }
        const auto n = self.socket->receive(static_cast<char*>(buffer), length);
        if (n > 0) return n;
        if (n == 0) return -EAGAIN;
        self.socket_closed = true;
        return 0;
    }
    static LIBSSH2_SEND_FUNC(transmit) {
        (void)flags;
        auto& self = of(abstract);
        if (!self.socket || socket != static_cast<libssh2_socket_t>(self.socket->handle())) {
            return agentIo(socket, const_cast<void*>(buffer), length, true);
        }
        const auto n = self.socket->send(std::string_view(static_cast<const char*>(buffer), length));
        if (n > 0) return n;
        if (n == 0) return -EAGAIN;
        self.socket_closed = true;
        return -EPIPE;
    }

    // --- Failures ---

    bool fail(SshError::Kind kind, std::string message) {
        if (!self.error_) {
            self.error_.kind    = kind;
            self.error_.message = printableText(message);
        }
        return false;
    }

    std::string lastError() const {
        if (!session) return {};
        char* message = nullptr;
        int length    = 0;
        libssh2_session_last_error(session, &message, &length, 0);
        return message ? printableText(std::string(message, static_cast<std::size_t>(std::max(length, 0))))
                       : std::string();
    }

    // A libssh2 call failed with `rc` while doing `what`.
    bool failSsh(int rc, const std::string& what) {
        if (self.error_) return false;
        if (socket_closed || rc == LIBSSH2_ERROR_SOCKET_DISCONNECT || rc == LIBSSH2_ERROR_SOCKET_RECV ||
            rc == LIBSSH2_ERROR_SOCKET_SEND) {
            return fail(SshError::Kind::Network, trf("The connection to {} broke while {}.", target.name, what));
        }
        if (rc == LIBSSH2_ERROR_SFTP_PROTOCOL && sftp) {
            const auto code = libssh2_sftp_last_error(sftp);
            if (code == LIBSSH2_FX_NO_SUCH_FILE || code == LIBSSH2_FX_NO_SUCH_PATH) {
                return fail(SshError::Kind::NotFound, trf("{}: not found while {}.", target.name, what));
            }
            if (code == LIBSSH2_FX_PERMISSION_DENIED) {
                return fail(SshError::Kind::Remote,
                            trf("{}: permission denied while {}: the user {} may not write there.", target.name, what,
                                target.user));
            }
            if (code == LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM || code == LIBSSH2_FX_QUOTA_EXCEEDED) {
                return fail(SshError::Kind::Remote,
                            trf("{}: the disk is full while {}: free some space on the machine.", target.name, what));
            }
            return fail(SshError::Kind::Remote, trf("{}: the server refused while {} (SFTP error {}).", target.name,
                                                    what, static_cast<unsigned long>(code)));
        }
        const auto detail = lastError();
        return fail(SshError::Kind::Remote,
                    trf("{}: failed while {} ({}).", target.name, what, detail.empty() ? std::to_string(rc) : detail));
    }

    // --- Waiting ---

    // Until the socket can go on as libssh2 waits for it, a moment at most. False, with the error, once the
    // connection is gone, the client cancelled, or `deadline` passed.
    bool wait(Clock::time_point deadline, const std::string& what) {
        if (self.cancel_ && *self.cancel_) return fail(SshError::Kind::Cancelled, TR("Cancelled."));
        if (socket_closed) {
            return fail(SshError::Kind::Network, trf("The connection to {} broke while {}.", target.name, what));
        }
        const auto now = Clock::now();
        if (now >= deadline) {
            return fail(SshError::Kind::Timeout,
                        trf("{} did not answer in time while {}: is the machine overloaded, or the network down?",
                            target.name, what));
        }
        const int directions = session ? libssh2_session_block_directions(session) : 0;
        bool in              = (directions & LIBSSH2_SESSION_BLOCK_INBOUND) != 0;
        const bool out       = (directions & LIBSSH2_SESSION_BLOCK_OUTBOUND) != 0;
        if (!in && !out) in = true;
        std::vector<net::Wait> waits{{socket->handle(), in, out}};
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        net::waitAny(waits, static_cast<int>(std::clamp<int64_t>(left, 1, WAIT_STEP_MS)));
        return true;
    }

    Clock::time_point deadline() const { return Clock::now() + target.timeout; }

    // Calls `f` until it does not answer EAGAIN: its result, or LIBSSH2_ERROR_TIMEOUT once wait() failed.
    template <typename F>
    int retry(F f, const std::string& what) {
        const auto until = deadline();
        while (true) {
            const int rc = f();
            if (rc != LIBSSH2_ERROR_EAGAIN) return rc;
            if (!wait(until, what)) return LIBSSH2_ERROR_TIMEOUT;
        }
    }

    // The same for the calls that give a pointer: nullptr with the error set when they fail.
    template <typename T, typename F>
    T* retryOpen(F f, const std::string& what) {
        const auto until = deadline();
        while (true) {
            if (T* p = f()) return p;
            const int rc = libssh2_session_last_errno(session);
            if (rc != LIBSSH2_ERROR_EAGAIN) {
                failSsh(rc, what);
                return nullptr;
            }
            if (!wait(until, what)) return nullptr;
        }
    }

    // --- The connection ---

    bool connect() {
        if (!startSsh()) return fail(SshError::Kind::Local, TR("The cryptography cannot start."));
        std::string error;
        const auto ms = static_cast<int>(target.timeout.count());
        const auto fd = net::connectHost(target.host, std::to_string(target.port), ms, error, self.cancel_);
        if (!fd) {
            if (self.cancel_ && *self.cancel_) return fail(SshError::Kind::Cancelled, TR("Cancelled."));
            return fail(SshError::Kind::Network,
                        trf("{} cannot be reached on port {} ({}): check the host name, that the machine is on and "
                            "that its SSH server runs.",
                            target.host, target.port, error));
        }
        socket  = std::make_unique<net::Socket>(*fd);
        session = libssh2_session_init_ex(&Impl::allocate, &Impl::release, &Impl::reallocate, this);
        if (!session) return fail(SshError::Kind::Local, TR("The SSH session cannot start."));
        libssh2_session_set_blocking(session, 0);
        libssh2_session_callback_set2(session, LIBSSH2_CALLBACK_RECV,
                                      reinterpret_cast<libssh2_cb_generic*>(&Impl::receive));
        libssh2_session_callback_set2(session, LIBSSH2_CALLBACK_SEND,
                                      reinterpret_cast<libssh2_cb_generic*>(&Impl::transmit));
        const auto banner = std::string("SSH-2.0-fastchessgui_") + FCG_VERSION;
        if (libssh2_session_banner_set(session, banner.c_str()) != 0 ||
            libssh2_session_method_pref(session, LIBSSH2_METHOD_KEX, KEX) != 0 ||
            libssh2_session_method_pref(session, LIBSSH2_METHOD_HOSTKEY, HOST_KEYS) != 0 ||
            libssh2_session_method_pref(session, LIBSSH2_METHOD_CRYPT_CS, CIPHERS) != 0 ||
            libssh2_session_method_pref(session, LIBSSH2_METHOD_CRYPT_SC, CIPHERS) != 0 ||
            libssh2_session_method_pref(session, LIBSSH2_METHOD_MAC_CS, MACS) != 0 ||
            libssh2_session_method_pref(session, LIBSSH2_METHOD_MAC_SC, MACS) != 0) {
            return fail(SshError::Kind::Local, TR("The SSH session cannot be set up."));
        }
        const auto handshake = TR("connecting");
        const int rc         = retry([&] { return libssh2_session_handshake(session, socket->handle()); }, handshake);
        if (rc == LIBSSH2_ERROR_TIMEOUT) return false;
        if (rc == LIBSSH2_ERROR_KEX_FAILURE) {
            return fail(SshError::Kind::Network,
                        trf("{} has no algorithm in common with fastchess-gui (it needs curve25519-sha256, an RSA host "
                            "key and AES): add an RSA host key to its SSH server (ssh-keygen -A).",
                            target.host));
        }
        if (rc != 0) return failSsh(rc, handshake);
        return checkHostKey() && authenticate();
    }

    bool checkHostKey() {
        std::size_t length = 0;
        int type           = 0;
        const char* key    = libssh2_session_hostkey(session, &length, &type);
        if (!key || type != LIBSSH2_HOSTKEY_TYPE_RSA) {
            return fail(SshError::Kind::HostKey, trf("{} did not show an RSA host key.", target.host));
        }
        const auto print = fingerprint(std::string_view(key, length));
        self.host_key_   = print;
        if (print.empty()) {
            return fail(SshError::Kind::HostKey, trf("The host key of {} cannot be checked.", target.host));
        }
        if (target.known_hosts.empty()) {
            return fail(SshError::Kind::HostKey,
                        trf("The host key of {} cannot be checked: nowhere to keep it.", target.host));
        }
        const auto known = knownHostKey(target.known_hosts, target.host, target.port);
        if (!known) {
            return fail(SshError::Kind::HostKey,
                        trf("{} cannot be read, so the host keys trusted before are unknown: nothing was sent to {}. "
                            "Repair the file, or delete it to trust the server's key again.",
                            target.known_hosts.string(), target.host));
        }
        if (known->empty()) {
            // Trusted on first use, and shown: the test of the connection tells it.
            if (!rememberHostKey(target.known_hosts, target.host, target.port, print)) {
                return fail(SshError::Kind::Local,
                            trf("The host key of {} cannot be kept in {}.", target.host, target.known_hosts.string()));
            }
            self.host_key_new_ = true;
            return true;
        }
        if (*known == print) return true;
        self.error_.new_key = print;
        return fail(SshError::Kind::HostKey,
                    trf("The host key of {} changed: {} now, {} before. Someone may be in the way; trust the new key "
                        "only if the machine was reinstalled or its owner says it changed.",
                        target.host, print, *known));
    }

    bool authenticate() {
        const auto what     = TR("logging in");
        const auto until    = deadline();
        const char* methods = nullptr;
        while (true) {
            methods = libssh2_userauth_list(session, target.user.c_str(), static_cast<unsigned>(target.user.size()));
            if (methods || libssh2_userauth_authenticated(session)) break;
            const int rc = libssh2_session_last_errno(session);
            if (rc != LIBSSH2_ERROR_EAGAIN) return failSsh(rc, what);
            if (!wait(until, what)) return false;
        }
        if (libssh2_userauth_authenticated(session)) return true;  // the server lets this user in as it is
        const std::string accepted(methods);
        if (accepted.find("publickey") == std::string::npos) {
            return fail(SshError::Kind::Auth,
                        trf("{} does not accept keys for user {} (it accepts: {}): turn on PubkeyAuthentication in "
                            "its SSH server's configuration.",
                            target.host, target.user, accepted.substr(0, 200)));
        }
        return target.auth == SshAuth::Agent ? agentLogin() : keyFileLogin();
    }

    bool agentLogin() {
        LIBSSH2_AGENT* agent = libssh2_agent_init(session);
        if (!agent) return fail(SshError::Kind::Local, TR("The SSH agent cannot be used."));
        struct Free {
            LIBSSH2_AGENT* a;
            ~Free() {
                libssh2_agent_disconnect(a);
                libssh2_agent_free(a);
            }
        } free_agent{agent};
        if (libssh2_agent_connect(agent) != 0) {
            return fail(SshError::Kind::Auth,
                        TR("No SSH agent answers on this computer: start one and add your key to it (ssh-add), or "
                           "log in with a key file."));
        }
        if (libssh2_agent_list_identities(agent) != 0) {
            return fail(SshError::Kind::Auth, TR("The SSH agent cannot list its keys."));
        }
        libssh2_agent_publickey* identity = nullptr;
        libssh2_agent_publickey* previous = nullptr;
        int keys                          = 0;
        while (libssh2_agent_get_identity(agent, &identity, previous) == 0) {
            ++keys;
            const int rc =
                retry([&] { return libssh2_agent_userauth(agent, target.user.c_str(), identity); }, TR("logging in"));
            if (rc == 0) return true;
            if (rc == LIBSSH2_ERROR_TIMEOUT || socket_closed) return failSsh(rc, TR("logging in"));
            previous = identity;
        }
        if (keys == 0) {
            return fail(SshError::Kind::Auth,
                        TR("The SSH agent holds no key: add yours to it (ssh-add), or log in with a key file."));
        }
        return fail(SshError::Kind::Auth,
                    trf("{} accepts none of the SSH agent's keys for user {}: add your public key to "
                        "~/.ssh/authorized_keys on the machine (on Windows, for an administrator: "
                        "C:\\ProgramData\\ssh\\administrators_authorized_keys).",
                        target.host, target.user));
    }

    bool keyFileLogin() {
        if (target.key_file.empty()) {
            return fail(SshError::Kind::Auth,
                        trf("No key file is chosen for {}: choose one, or log in with the SSH agent.", target.name));
        }
        auto data = readFile(target.key_file, 1 << 20);
        if (!data) {
            return fail(SshError::Kind::Local, trf("The key file {} cannot be read.", target.key_file.string()));
        }
        const char* passphrase = target.passphrase.empty() ? nullptr : target.passphrase.c_str();
        const int rc           = retry(
            [&] {
                return libssh2_userauth_publickey_frommemory(session, target.user.c_str(), target.user.size(), nullptr,
                                                             0, data->data(), data->size(), passphrase);
            },
            TR("logging in"));
        mbedtls_platform_zeroize(data->data(), data->size());
        if (rc == 0) return true;
        if (rc == LIBSSH2_ERROR_TIMEOUT) return false;
        const auto detail = lastError();
        if (rc == LIBSSH2_ERROR_KEYFILE_AUTH_FAILED) {
            return fail(SshError::Kind::Passphrase,
                        target.passphrase.empty()
                            ? trf("The key file {} needs its passphrase.", target.key_file.string())
                            : trf("The passphrase of the key file {} is not the right one.", target.key_file.string()));
        }
        if (rc == LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED || rc == LIBSSH2_ERROR_AUTHENTICATION_FAILED) {
            return fail(SshError::Kind::Auth,
                        trf("{} refused the key {} for user {}: add its public key ({}.pub) to ~/.ssh/authorized_keys "
                            "on the machine (on Windows, for an administrator: "
                            "C:\\ProgramData\\ssh\\administrators_authorized_keys).",
                            target.host, target.key_file.string(), target.user, target.key_file.string()));
        }
        if (rc == LIBSSH2_ERROR_FILE || rc == LIBSSH2_ERROR_METHOD_NOT_SUPPORTED) {
            return fail(SshError::Kind::Auth,
                        trf("The key file {} cannot be used: {}", target.key_file.string(), detail));
        }
        return failSsh(rc, TR("logging in"));
    }

    void close() {
        if (sftp) {
            const auto until = Clock::now() + std::chrono::seconds(1);
            while (libssh2_sftp_shutdown(sftp) == LIBSSH2_ERROR_EAGAIN && Clock::now() < until && !socket_closed) {
                std::vector<net::Wait> waits{{socket->handle(), true, false}};
                net::waitAny(waits, 20);
            }
            sftp = nullptr;
        }
        if (session) {
            if (!socket_closed) {
                const auto until = Clock::now() + std::chrono::seconds(1);
                while (libssh2_session_disconnect(session, "bye") == LIBSSH2_ERROR_EAGAIN && Clock::now() < until &&
                       !socket_closed) {
                    std::vector<net::Wait> waits{{socket->handle(), false, true}};
                    net::waitAny(waits, 20);
                }
            }
            fcg_libssh2_session_end(session);
            for (int i = 0; i < 100 && libssh2_session_free(session) == LIBSSH2_ERROR_EAGAIN; ++i) {
            }
            session = nullptr;
        }
        socket.reset();
    }

    // --- Channels ---

    LIBSSH2_CHANNEL* openChannel(const std::string& command) {
        const auto what = TR("starting a command");
        auto* channel   = retryOpen<LIBSSH2_CHANNEL>([&] { return libssh2_channel_open_session(session); }, what);
        if (!channel) return nullptr;
        const int rc = retry(
            [&] {
                return libssh2_channel_process_startup(channel, "exec", 4, command.c_str(),
                                                       static_cast<unsigned>(command.size()));
            },
            what);
        if (rc != 0) {
            if (rc != LIBSSH2_ERROR_TIMEOUT) failSsh(rc, what);
            freeChannel(channel);
            return nullptr;
        }
        return channel;
    }

    void freeChannel(LIBSSH2_CHANNEL* channel) {
        if (!channel) return;
        if (!socket_closed) {
            const auto until = Clock::now() + std::chrono::seconds(2);
            while (libssh2_channel_close(channel) == LIBSSH2_ERROR_EAGAIN && Clock::now() < until && !socket_closed) {
                std::vector<net::Wait> waits{{socket->handle(), true, true}};
                net::waitAny(waits, 20);
            }
        }
        libssh2_channel_free(channel);
    }

    // Writes all of `data`, as long as the channel takes it.
    bool writeAll(LIBSSH2_CHANNEL* channel, std::string_view data, const std::string& what) {
        auto until = deadline();
        while (!data.empty()) {
            const auto n = libssh2_channel_write(channel, data.data(), data.size());
            if (n == LIBSSH2_ERROR_EAGAIN) {
                if (!wait(until, what)) return false;
                continue;
            }
            if (n < 0) return failSsh(static_cast<int>(n), what);
            data.remove_prefix(static_cast<std::size_t>(n));
            until = deadline();
        }
        return true;
    }

    // Reads what is there of both streams, `out` and `err` (nullptr: dropped), `max` bytes kept of each. True when
    // something came; `ended` once the program closed its output.
    bool readSome(LIBSSH2_CHANNEL* channel, std::string* out, std::string* err, std::size_t max, bool& ended,
                  const std::string& what) {
        char buffer[16384];
        bool moved = false;
        for (int stream = 0; stream < 2; ++stream) {
            std::string* into = stream == 0 ? out : err;
            while (true) {
                const auto n = libssh2_channel_read_ex(channel, stream, buffer, sizeof buffer);
                if (n == LIBSSH2_ERROR_EAGAIN) break;
                if (n < 0) {
                    failSsh(static_cast<int>(n), what);
                    ended = true;
                    return moved;
                }
                if (n == 0) break;
                moved = true;
                if (into && into->size() < max) {
                    into->append(buffer, std::min(static_cast<std::size_t>(n), max - into->size()));
                }
            }
        }
        if (libssh2_channel_eof(channel) == 1) ended = true;
        return moved;
    }

    // --- SFTP ---

    bool startSftp() {
        if (sftp) return true;
        sftp = retryOpen<LIBSSH2_SFTP>([&] { return libssh2_sftp_init(session); }, TR("starting SFTP"));
        if (!sftp && self.error_.kind == SshError::Kind::Remote) {
            self.error_.message =
                trf("{} does not offer SFTP: turn on its sftp subsystem (Subsystem sftp in its SSH "
                    "server's configuration).",
                    target.name);
        }
        return sftp != nullptr;
    }

    LIBSSH2_SFTP_HANDLE* openFile(const std::string& path, unsigned long flags, long mode, int type,
                                  const std::string& what) {
        return retryOpen<LIBSSH2_SFTP_HANDLE>(
            [&] {
                return libssh2_sftp_open_ex(sftp, path.c_str(), static_cast<unsigned>(path.size()), flags, mode, type);
            },
            what);
    }

    void closeFile(LIBSSH2_SFTP_HANDLE* handle) {
        if (!handle) return;
        const auto until = Clock::now() + target.timeout;
        while (libssh2_sftp_close_handle(handle) == LIBSSH2_ERROR_EAGAIN && Clock::now() < until && !socket_closed) {
            std::vector<net::Wait> waits{{socket->handle(), true, true}};
            net::waitAny(waits, 20);
        }
    }

    static RemoteEntry entry(const std::string& name, const LIBSSH2_SFTP_ATTRIBUTES& attrs) {
        RemoteEntry e;
        e.name = name;
        if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) e.size = attrs.filesize;
        if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
            e.perms     = static_cast<unsigned>(attrs.permissions & 07777);
            e.directory = LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
            e.regular   = LIBSSH2_SFTP_S_ISREG(attrs.permissions);
        }
        if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) e.mtime = static_cast<int64_t>(attrs.mtime);
        return e;
    }
};

// ---------------------------------------------------------------------------------------------------------------

SshClient::SshClient(const std::atomic_bool* cancel) : impl_(std::make_unique<Impl>(*this)), cancel_(cancel) {}

SshClient::~SshClient() = default;

bool SshClient::connect(const SshTarget& target) {
    disconnect();
    impl_  = std::make_unique<Impl>(*this);
    error_ = {};
    host_key_.clear();
    host_key_new_ = false;
    impl_->target = target;
    if (impl_->target.name.empty()) impl_->target.name = target.host;
    if (!impl_->connect()) {
        impl_->close();
        return false;
    }
    return true;
}

bool SshClient::connected() const noexcept { return impl_ && impl_->session && !impl_->socket_closed; }

void SshClient::disconnect() {
    if (impl_) impl_->close();
}

std::optional<SshClient::Result> SshClient::run(const std::string& command, std::chrono::milliseconds limit,
                                                const std::string& input, std::size_t max_output) {
    error_ = {};
    if (!connected()) {
        impl_->fail(SshError::Kind::Network, trf("Not connected to {}.", impl_->target.name));
        return std::nullopt;
    }
    const auto what = trf("running {}", command.substr(0, 60));
    auto* channel   = impl_->openChannel(command);
    if (!channel) return std::nullopt;
    struct Free {
        Impl& impl;
        LIBSSH2_CHANNEL* c;
        ~Free() { impl.freeChannel(c); }
    } free_channel{*impl_, channel};
    if (!input.empty() && !impl_->writeAll(channel, input, what)) return std::nullopt;
    if (impl_->retry([&] { return libssh2_channel_send_eof(channel); }, what) == LIBSSH2_ERROR_TIMEOUT) {
        return std::nullopt;
    }
    Result result;
    bool ended       = false;
    const auto until = Clock::now() + limit;
    while (!ended) {
        const bool moved = impl_->readSome(channel, &result.out, &result.err, max_output, ended, what);
        if (error_) return std::nullopt;
        if (ended || moved) continue;
        if (!impl_->wait(until, what)) return std::nullopt;
    }
    // The exit status comes with the channel's close.
    if (impl_->retry([&] { return libssh2_channel_close(channel); }, what) == LIBSSH2_ERROR_TIMEOUT) {
        return std::nullopt;
    }
    impl_->retry([&] { return libssh2_channel_wait_closed(channel); }, what);
    error_            = {};
    result.exit_code  = libssh2_channel_get_exit_status(channel);
    char* signal      = nullptr;
    std::size_t sig_n = 0;
    libssh2_channel_get_exit_signal(channel, &signal, &sig_n, nullptr, nullptr, nullptr, nullptr);
    if (signal) {
        result.exit_code = -1;
        libssh2_free(impl_->session, signal);
    }
    return result;
}

std::unique_ptr<SshClient::Channel> SshClient::start(const std::string& command) {
    error_ = {};
    if (!connected()) {
        impl_->fail(SshError::Kind::Network, trf("Not connected to {}.", impl_->target.name));
        return nullptr;
    }
    auto* channel = impl_->openChannel(command);
    if (!channel) return nullptr;
    return std::unique_ptr<Channel>(new Channel(*this, channel));
}

SshClient::Channel::~Channel() {
    if (client_.impl_) client_.impl_->freeChannel(static_cast<LIBSSH2_CHANNEL*>(channel_));
}

bool SshClient::Channel::write(std::string_view data) {
    return client_.impl_->writeAll(static_cast<LIBSSH2_CHANNEL*>(channel_), data, TR("writing to a program"));
}

std::optional<std::string> SshClient::Channel::read(std::chrono::milliseconds wait) {
    auto* channel = static_cast<LIBSSH2_CHANNEL*>(channel_);
    auto& impl    = *client_.impl_;
    std::string out;
    const auto until = Clock::now() + wait;
    const auto what  = TR("reading from a program");
    while (true) {
        bool ended       = false;
        const bool moved = impl.readSome(channel, &out, &err_, std::size_t(1) << 20, ended, what);
        if (err_.size() > 4096) err_.resize(4096);
        if (client_.error_) return out.empty() ? std::nullopt : std::optional<std::string>(out);
        if (ended) eof_ = true;
        if (!out.empty() || eof_) break;
        if (moved) continue;
        if (Clock::now() >= until) break;
        // Waiting only as long as asked: no failure when nothing came meanwhile.
        if (!impl.wait(until, what)) {
            if (client_.error_.kind == SshError::Kind::Timeout) client_.error_ = {};
            break;
        }
    }
    if (out.empty() && eof_) return std::nullopt;
    return out;
}

void SshClient::Channel::closeInput() {
    client_.impl_->retry([&] { return libssh2_channel_send_eof(static_cast<LIBSSH2_CHANNEL*>(channel_)); },
                         TR("writing to a program"));
}

int SshClient::Channel::exitCode() {
    auto* channel = static_cast<LIBSSH2_CHANNEL*>(channel_);
    if (!eof_) return -1;
    return libssh2_channel_get_exit_status(channel);
}

std::optional<RemoteEntry> SshClient::stat(const std::string& path) {
    error_ = {};
    if (!connected() || !impl_->startSftp()) return std::nullopt;
    LIBSSH2_SFTP_ATTRIBUTES attrs{};
    const auto what = trf("looking at {}", path);
    const int rc    = impl_->retry(
        [&] {
            return libssh2_sftp_stat_ex(impl_->sftp, path.c_str(), static_cast<unsigned>(path.size()),
                                        LIBSSH2_SFTP_STAT, &attrs);
        },
        what);
    if (rc != 0) {
        if (rc != LIBSSH2_ERROR_TIMEOUT) impl_->failSsh(rc, what);
        return std::nullopt;
    }
    return Impl::entry(remoteName(path), attrs);
}

std::optional<std::vector<RemoteEntry>> SshClient::list(const std::string& dir) {
    error_ = {};
    if (!connected() || !impl_->startSftp()) return std::nullopt;
    const auto what = trf("listing {}", dir);
    auto* handle    = impl_->openFile(dir, 0, 0, LIBSSH2_SFTP_OPENDIR, what);
    if (!handle) return std::nullopt;
    std::vector<RemoteEntry> out;
    char name[1024];
    while (true) {
        LIBSSH2_SFTP_ATTRIBUTES attrs{};
        const int n =
            impl_->retry([&] { return libssh2_sftp_readdir_ex(handle, name, sizeof name, nullptr, 0, &attrs); }, what);
        if (n == 0) break;
        if (n < 0) {
            if (n != LIBSSH2_ERROR_TIMEOUT) impl_->failSsh(n, what);
            impl_->closeFile(handle);
            return std::nullopt;
        }
        const std::string file(name, static_cast<std::size_t>(n));
        if (file == "." || file == "..") continue;
        out.push_back(Impl::entry(file, attrs));
    }
    impl_->closeFile(handle);
    return out;
}

std::optional<std::string> SshClient::realPath(const std::string& path) {
    error_ = {};
    if (!connected() || !impl_->startSftp()) return std::nullopt;
    char target[2048];
    const auto what = trf("looking at {}", path);
    const int n     = impl_->retry(
        [&] {
            return libssh2_sftp_symlink_ex(impl_->sftp, path.c_str(), static_cast<unsigned>(path.size()), target,
                                           sizeof target, LIBSSH2_SFTP_REALPATH);
        },
        what);
    if (n < 0) {
        if (n != LIBSSH2_ERROR_TIMEOUT) impl_->failSsh(n, what);
        return std::nullopt;
    }
    return std::string(target, static_cast<std::size_t>(n));
}

bool SshClient::makeDirs(const std::string& path) {
    error_ = {};
    if (!connected() || !impl_->startSftp()) return false;
    // Each folder of the path from the first, made unless it is there.
    std::string prefix;
    std::size_t at = 0;
    while (at <= path.size()) {
        auto next = path.find_first_of("/\\", at);
        if (next == std::string::npos) next = path.size();
        prefix = path.substr(0, next);
        at     = next + 1;
        if (prefix.empty() || prefix == "/" || prefix == "." ||
            (prefix.size() <= 3 && prefix.back() == ':')) {  // "C:", "/C:"
            continue;
        }
        if (const auto e = stat(prefix)) {
            if (!e->directory) {
                return impl_->fail(SshError::Kind::Remote,
                                   trf("{}: {} is a file, not a folder.", impl_->target.name, prefix));
            }
            continue;
        }
        if (error_.kind != SshError::Kind::NotFound) return false;
        error_          = {};
        const auto what = trf("making the folder {}", prefix);
        const int rc    = impl_->retry(
            [&] {
                return libssh2_sftp_mkdir_ex(impl_->sftp, prefix.c_str(), static_cast<unsigned>(prefix.size()), 0755);
            },
            what);
        if (rc != 0) {
            if (rc != LIBSSH2_ERROR_TIMEOUT) impl_->failSsh(rc, what);
            return false;
        }
    }
    return true;
}

bool SshClient::remove(const std::string& path) {
    error_ = {};
    if (!connected() || !impl_->startSftp()) return false;
    const auto what = trf("removing {}", path);
    const int rc    = impl_->retry(
        [&] { return libssh2_sftp_unlink_ex(impl_->sftp, path.c_str(), static_cast<unsigned>(path.size())); }, what);
    if (rc == 0) return true;
    if (rc == LIBSSH2_ERROR_SFTP_PROTOCOL) {
        const auto code = libssh2_sftp_last_error(impl_->sftp);
        if (code == LIBSSH2_FX_NO_SUCH_FILE || code == LIBSSH2_FX_NO_SUCH_PATH) return true;
    }
    if (rc != LIBSSH2_ERROR_TIMEOUT) impl_->failSsh(rc, what);
    return false;
}

bool SshClient::rename(const std::string& from, const std::string& to) {
    // SFTP version 3 (OpenSSH's) does not replace a file: the one there is removed first.
    if (!remove(to)) return false;
    const auto what = trf("renaming {}", from);
    const int rc    = impl_->retry(
        [&] {
            return libssh2_sftp_rename_ex(
                impl_->sftp, from.c_str(), static_cast<unsigned>(from.size()), to.c_str(),
                static_cast<unsigned>(to.size()),
                LIBSSH2_SFTP_RENAME_OVERWRITE | LIBSSH2_SFTP_RENAME_ATOMIC | LIBSSH2_SFTP_RENAME_NATIVE);
        },
        what);
    if (rc != 0) {
        if (rc != LIBSSH2_ERROR_TIMEOUT) impl_->failSsh(rc, what);
        return false;
    }
    return true;
}

bool SshClient::setPermissions(const std::string& path, unsigned perms) {
    error_ = {};
    if (!connected() || !impl_->startSftp()) return false;
    LIBSSH2_SFTP_ATTRIBUTES attrs{};
    attrs.flags       = LIBSSH2_SFTP_ATTR_PERMISSIONS;
    attrs.permissions = perms;
    const auto what   = trf("setting the permissions of {}", path);
    const int rc      = impl_->retry(
        [&] {
            return libssh2_sftp_stat_ex(impl_->sftp, path.c_str(), static_cast<unsigned>(path.size()),
                                        LIBSSH2_SFTP_SETSTAT, &attrs);
        },
        what);
    if (rc != 0) {
        if (rc != LIBSSH2_ERROR_TIMEOUT) impl_->failSsh(rc, what);
        return false;
    }
    return true;
}

bool SshClient::upload(const fs::path& local, const std::string& remote, const Progress& progress, unsigned perms) {
    error_ = {};
    if (!connected() || !impl_->startSftp()) return false;
    std::error_code ec;
    const auto total = fs::file_size(local, ec);
    if (ec) return impl_->fail(SshError::Kind::Local, trf("{} cannot be read.", local.string()));
    std::ifstream in(local, std::ios::binary);
    if (!in) return impl_->fail(SshError::Kind::Local, trf("{} cannot be read.", local.string()));

    // What an earlier copy left, unless it is longer than the file (another file of the same name).
    const auto part = remote + ".part";
    uint64_t done   = 0;
    if (const auto there = stat(part)) {
        done = there->size <= total ? there->size : 0;
    } else if (error_.kind != SshError::Kind::NotFound) {
        return false;
    }
    error_          = {};
    const auto what = trf("copying {} to {}", local.filename().string(), impl_->target.name);
    auto* handle    = impl_->openFile(part, LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | (done == 0 ? LIBSSH2_FXF_TRUNC : 0),
                                      static_cast<long>(perms), LIBSSH2_SFTP_OPENFILE, what);
    if (!handle) return false;
    libssh2_sftp_seek64(handle, done);
    in.seekg(static_cast<std::streamoff>(done));
    std::string piece(TRANSFER_PIECE, '\0');
    bool ok = true;
    if (progress && !progress(done, total)) {
        ok = impl_->fail(SshError::Kind::Cancelled, TR("Cancelled."));
    }
    while (ok && done < total) {
        const auto want = static_cast<std::size_t>(std::min<uint64_t>(TRANSFER_PIECE, total - done));
        in.read(piece.data(), static_cast<std::streamsize>(want));
        if (static_cast<std::size_t>(in.gcount()) != want) {
            ok = impl_->fail(SshError::Kind::Local, trf("{} cannot be read.", local.string()));
            break;
        }
        std::string_view rest(piece.data(), want);
        auto until = impl_->deadline();
        while (ok && !rest.empty()) {
            const auto n = libssh2_sftp_write(handle, rest.data(), rest.size());
            if (n == LIBSSH2_ERROR_EAGAIN) {
                ok = impl_->wait(until, what);
                continue;
            }
            if (n < 0) {
                ok = impl_->failSsh(static_cast<int>(n), what);
                break;
            }
            rest.remove_prefix(static_cast<std::size_t>(n));
            done += static_cast<uint64_t>(n);
            until = impl_->deadline();
            if (progress && !progress(done, total)) ok = impl_->fail(SshError::Kind::Cancelled, TR("Cancelled."));
        }
    }
    impl_->closeFile(handle);
    if (!ok) return false;
    // Whole: its size is the file's, then it takes its name.
    const auto copied = stat(part);
    if (!copied) return false;
    if (copied->size != total) {
        return impl_->fail(SshError::Kind::Remote,
                           trf("The copy of {} on {} has {} bytes instead of {}: copy it again.",
                               local.filename().string(), impl_->target.name, copied->size, total));
    }
    return rename(part, remote);
}

bool SshClient::download(const std::string& remote, const fs::path& local, const Progress& progress) {
    error_ = {};
    if (!connected() || !impl_->startSftp()) return false;
    const auto there = stat(remote);
    if (!there) return false;
    const auto total = there->size;
    auto part        = local;
    part += ".part";
    std::error_code ec;
    uint64_t done = 0;
    if (fs::exists(part, ec)) {
        const auto size = fs::file_size(part, ec);
        done            = !ec && size <= total ? size : 0;
    }
    std::ofstream out(part, done > 0 ? std::ios::binary | std::ios::app : std::ios::binary | std::ios::trunc);
    if (!out) return impl_->fail(SshError::Kind::Local, trf("{} cannot be written.", part.string()));
    const auto what = trf("copying {} from {}", remoteName(remote), impl_->target.name);
    auto* handle    = impl_->openFile(remote, LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE, what);
    if (!handle) return false;
    libssh2_sftp_seek64(handle, done);
    std::string piece(TRANSFER_PIECE, '\0');
    bool ok    = !progress || progress(done, total);
    auto until = impl_->deadline();
    if (!ok) impl_->fail(SshError::Kind::Cancelled, TR("Cancelled."));
    while (ok && done < total) {
        const auto n = libssh2_sftp_read(handle, piece.data(), piece.size());
        if (n == LIBSSH2_ERROR_EAGAIN) {
            ok = impl_->wait(until, what);
            continue;
        }
        if (n < 0) {
            ok = impl_->failSsh(static_cast<int>(n), what);
            break;
        }
        if (n == 0) break;
        out.write(piece.data(), n);
        if (!out) {
            ok = impl_->fail(SshError::Kind::Local, trf("{} cannot be written.", part.string()));
            break;
        }
        done += static_cast<uint64_t>(n);
        until = impl_->deadline();
        if (progress && !progress(done, total)) ok = impl_->fail(SshError::Kind::Cancelled, TR("Cancelled."));
    }
    impl_->closeFile(handle);
    out.close();
    if (!ok) return false;
    if (done != total) {
        return impl_->fail(SshError::Kind::Remote, trf("The copy of {} from {} has {} bytes instead of {}.",
                                                       remoteName(remote), impl_->target.name, done, total));
    }
    fs::rename(part, local, ec);
    if (ec) return impl_->fail(SshError::Kind::Local, trf("{} cannot be written.", local.string()));
    return true;
}

std::optional<uint64_t> SshClient::fetch(const std::string& remote, const fs::path& local, uint64_t from) {
    error_ = {};
    if (!connected() || !impl_->startSftp()) return std::nullopt;
    std::ofstream out(local, from > 0 ? std::ios::binary | std::ios::app : std::ios::binary | std::ios::trunc);
    if (!out) {
        impl_->fail(SshError::Kind::Local, trf("{} cannot be written.", local.string()));
        return std::nullopt;
    }
    const auto what = trf("copying {} from {}", remoteName(remote), impl_->target.name);
    auto* handle    = impl_->openFile(remote, LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE, what);
    if (!handle) return std::nullopt;
    libssh2_sftp_seek64(handle, from);
    std::string piece(TRANSFER_PIECE, '\0');
    uint64_t copied = 0;
    bool ok         = true;
    auto until      = impl_->deadline();
    while (ok) {
        const auto n = libssh2_sftp_read(handle, piece.data(), piece.size());
        if (n == LIBSSH2_ERROR_EAGAIN) {
            ok = impl_->wait(until, what);
            continue;
        }
        if (n < 0) {
            ok = impl_->failSsh(static_cast<int>(n), what);
            break;
        }
        if (n == 0) break;
        out.write(piece.data(), n);
        if (!out) {
            ok = impl_->fail(SshError::Kind::Local, trf("{} cannot be written.", local.string()));
            break;
        }
        copied += static_cast<uint64_t>(n);
        until = impl_->deadline();
    }
    impl_->closeFile(handle);
    if (!ok) return std::nullopt;
    return copied;
}

}  // namespace fcg
