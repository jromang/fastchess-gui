#include "ssh_server.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>

#include <psa/crypto.h>

#include "net.hpp"
#include "ssh_wire.hpp"

namespace fcg::ssh {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint8_t USERAUTH_PK_OK = 60;

// SFTP version 3 (draft-ietf-secsh-filexfer-02).
enum Sftp : uint8_t {
    FXP_INIT     = 1,
    FXP_VERSION  = 2,
    FXP_OPEN     = 3,
    FXP_CLOSE    = 4,
    FXP_READ     = 5,
    FXP_WRITE    = 6,
    FXP_LSTAT    = 7,
    FXP_FSTAT    = 8,
    FXP_SETSTAT  = 9,
    FXP_FSETSTAT = 10,
    FXP_OPENDIR  = 11,
    FXP_READDIR  = 12,
    FXP_REMOVE   = 13,
    FXP_MKDIR    = 14,
    FXP_RMDIR    = 15,
    FXP_REALPATH = 16,
    FXP_STAT     = 17,
    FXP_RENAME   = 18,
    FXP_STATUS   = 101,
    FXP_HANDLE   = 102,
    FXP_DATA     = 103,
    FXP_NAME     = 104,
    FXP_ATTRS    = 105,
};
enum SftpStatus : uint32_t { FX_OK = 0, FX_EOF = 1, FX_NO_SUCH_FILE = 2, FX_FAILURE = 4, FX_OP_UNSUPPORTED = 8 };

std::runtime_error failure(const std::string& what) { return std::runtime_error("mock SSH server: " + what); }

// The server's RSA host key, made once.
struct HostKey {
    mbedtls_svc_key_id_t id = 0;
    std::string blob;

    HostKey() {
        if (!startCrypto()) throw failure("no cryptography");
        psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_type(&a, PSA_KEY_TYPE_RSA_KEY_PAIR);
        psa_set_key_bits(&a, 2048);
        psa_set_key_algorithm(&a, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_ANY_HASH));
        psa_set_key_usage_flags(&a, PSA_KEY_USAGE_SIGN_MESSAGE);
        if (psa_generate_key(&a, &id) != PSA_SUCCESS) throw failure("no host key");
        std::string der(600, '\0');
        std::size_t n = 0;
        if (psa_export_public_key(id, reinterpret_cast<uint8_t*>(der.data()), der.size(), &n) != PSA_SUCCESS) {
            throw failure("no public key");
        }
        der.resize(n);
        // RSAPublicKey ::= SEQUENCE { modulus INTEGER, publicExponent INTEGER }
        std::size_t at    = 0;
        const auto length = [&] {
            std::size_t len = static_cast<unsigned char>(der[at++]);
            if (len & 0x80) {
                const auto bytes = len & 0x7F;
                len              = 0;
                for (std::size_t i = 0; i < bytes; ++i) len = len << 8 | static_cast<unsigned char>(der[at++]);
            }
            return len;
        };
        const auto integer = [&] {
            ++at;  // 0x02
            const auto len = length();
            const auto v   = der.substr(at, len);
            at += len;
            return v;
        };
        ++at;  // 0x30
        length();
        const auto modulus  = integer();
        const auto exponent = integer();
        blob                = Writer().string("ssh-rsa").mpint(exponent).mpint(modulus).take();
    }
    std::string sign(const std::string& data) const {
        std::string sig(512, '\0');
        std::size_t n = 0;
        if (psa_sign_message(id, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_512),
                             reinterpret_cast<const uint8_t*>(data.data()), data.size(),
                             reinterpret_cast<uint8_t*>(sig.data()), sig.size(), &n) != PSA_SUCCESS) {
            throw failure("cannot sign");
        }
        sig.resize(n);
        return Writer().string("rsa-sha2-512").string(sig).take();
    }
};

const HostKey& hostKey() {
    static const HostKey key;
    return key;
}

std::string derLength(std::size_t n) {
    std::string out;
    if (n < 0x80) {
        out += static_cast<char>(n);
    } else if (n < 0x100) {
        out += '\x81';
        out += static_cast<char>(n);
    } else {
        out += '\x82';
        out += static_cast<char>(n >> 8);
        out += static_cast<char>(n & 0xFF);
    }
    return out;
}

// An RSA signature of the user's (algo rsa-sha2-256 or -512) over `data`, with the key of `blob`.
bool verifyRsa(const std::string& blob, const std::string& algo, const std::string& data, const std::string& sig) {
    Reader r(blob);
    if (r.string() != "ssh-rsa") return false;
    const std::string e(r.string()), n(r.string());
    if (!r.ok()) return false;
    // An mpint's bytes are a DER INTEGER's.
    const auto integer     = [](const std::string& v) { return "\x02" + derLength(v.size()) + v; };
    const auto body        = integer(n) + integer(e);
    const auto der         = "\x30" + derLength(body.size()) + body;
    psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&a, PSA_KEY_TYPE_RSA_PUBLIC_KEY);
    psa_set_key_algorithm(&a, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_ANY_HASH));
    psa_set_key_usage_flags(&a, PSA_KEY_USAGE_VERIFY_MESSAGE);
    mbedtls_svc_key_id_t key = 0;
    if (psa_import_key(&a, reinterpret_cast<const uint8_t*>(der.data()), der.size(), &key) != PSA_SUCCESS) {
        return false;
    }
    const auto alg = algo == "rsa-sha2-512" ? PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_512)
                                            : PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256);
    const bool ok  = psa_verify_message(key, alg, reinterpret_cast<const uint8_t*>(data.data()), data.size(),
                                        reinterpret_cast<const uint8_t*>(sig.data()), sig.size()) == PSA_SUCCESS;
    psa_destroy_key(key);
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
            continue;
        acc = acc << 6 | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    return out;
}

uint64_t u64(Reader& r) {
    const uint64_t high = r.u32();
    return high << 32 | r.u32();
}

}  // namespace

std::string publicKeyBlob(const std::string& line) {
    const auto first = line.find(' ');
    if (first == std::string::npos) return {};
    auto end = line.find(' ', first + 1);
    if (end == std::string::npos) end = line.size();
    return decodeBase64(std::string_view(line).substr(first + 1, end - first - 1));
}

struct MockServer::Listener {
    net::TcpListener tcp;
};

// ---------------------------------------------------------------------------------------------------------------

struct MockServer::Connection {
    MockServer& server;
    std::unique_ptr<net::Socket> socket;
    std::string in, out, v_c, session_id;
    PacketCipher rx, tx;
    uint32_t seq_rx = 0, seq_tx = 0;
    uint64_t written_here = 0;
    bool gone             = false;

    struct Channel {
        uint32_t peer                                   = 0;
        uint64_t peer_window                            = 0;
        uint32_t peer_max                               = 0;
        enum class Kind { None, Exec, Talk, Sftp } kind = Kind::None;
        std::string out, err;  // waiting for the peer's window
        bool finished    = false;
        int status       = 0;
        bool closed_sent = false;
        std::string sftp_in;
        std::function<std::string(const std::string&, bool, bool&)> talk;
    };
    std::map<uint32_t, Channel> channels;
    uint32_t next_channel = 0;

    struct Handle {
        fs::path path;
        std::unique_ptr<std::fstream> file;
        std::vector<fs::directory_entry> entries;
        std::size_t next = 0;
    };
    std::map<std::string, Handle> handles;
    int next_handle = 0;

    Connection(MockServer& s, net::Handle fd) : server(s), socket(std::make_unique<net::Socket>(fd)) {}

    // --- The packets ---

    void flushOut() {
        const auto until = Clock::now() + 5s;
        while (!out.empty() && !gone) {
            if (Clock::now() > until) throw failure("cannot send");
            std::vector<net::Wait> waits{{socket->handle(), false, true}};
            net::waitAny(waits, 50);
            const auto n = socket->send(out);
            if (n < 0) {
                gone = true;
                return;
            }
            out.erase(0, static_cast<std::size_t>(n));
        }
    }
    void send(const std::string& payload) {
        out += tx.seal(payload, seq_tx++);
        flushOut();
    }
    // The next message; nullopt once the client is gone or the server stops.
    std::optional<std::string> next() {
        while (!gone && !server.quit_) {
            std::string payload;
            const auto r = rx.open(in, seq_rx, payload);
            if (r == PacketCipher::Open::Bad) throw failure("bad packet from the client");
            if (r == PacketCipher::Open::Packet) {
                ++seq_rx;
                return payload;
            }
            std::vector<net::Wait> waits{{socket->handle(), true, false}};
            net::waitAny(waits, 50);
            if (waits[0].can_read && socket->receive(in, 65536) < 0) gone = true;
        }
        return std::nullopt;
    }
    std::string nextOf(uint8_t type) {
        auto m = next();
        if (!m) throw failure("the client left");
        if (static_cast<uint8_t>((*m)[0]) != type) {
            throw failure("expected message " + std::to_string(type) + ", got " + std::to_string(uint8_t((*m)[0])));
        }
        return *m;
    }

    void kex() {
        const auto lists = Writer()
                               .byte(KEXINIT)
                               .raw(randomBytes(16))
                               .string("curve25519-sha256,kex-strict-s-v00@openssh.com")
                               .string("rsa-sha2-512,rsa-sha2-256")
                               .string("aes256-gcm@openssh.com")
                               .string("aes256-gcm@openssh.com")
                               .string("hmac-sha2-256")
                               .string("hmac-sha2-256")
                               .string("none")
                               .string("none")
                               .string("")
                               .string("")
                               .boolean(false)
                               .u32(0)
                               .take();
        send(lists);
        const auto i_c  = nextOf(KEXINIT);
        const auto init = nextOf(KEX_ECDH_INIT);
        Reader r(std::string_view(init).substr(1));
        const std::string q_c(r.string());
        X25519 ecdh;
        const auto secret = ecdh.agree(q_c);
        if (!secret) throw failure("bad client key");
        const std::string version = "SSH-2.0-MockServer_1.0";
        const auto k              = Writer().mpint(*secret).take();
        const auto h              = sha256(Writer()
                                               .string(v_c)
                                               .string(version)
                                               .string(i_c)
                                               .string(lists)
                                               .string(hostKey().blob)
                                               .string(q_c)
                                               .string(ecdh.publicKey())
                                               .raw(k)
                                               .data());
        session_id                = h;
        send(Writer()
                 .byte(KEX_ECDH_REPLY)
                 .string(hostKey().blob)
                 .string(ecdh.publicKey())
                 .string(hostKey().sign(h))
                 .take());
        send(Writer().byte(NEWKEYS).take());
        const auto derive = [&](char letter, std::size_t size) { return deriveKey(k, h, letter, session_id, size); };
        const std::string cipher = "aes256-gcm@openssh.com";
        if (!tx.start(cipher, "", derive('D', PacketCipher::keySize(cipher)), derive('B', PacketCipher::ivSize(cipher)),
                      "")
                 .empty()) {
            throw failure("keys");
        }
        seq_tx = 0;  // strict key exchange
        nextOf(NEWKEYS);
        if (!rx.start(cipher, "", derive('C', PacketCipher::keySize(cipher)), derive('A', PacketCipher::ivSize(cipher)),
                      "")
                 .empty()) {
            throw failure("keys");
        }
        seq_rx = 0;
        // As OpenSSH: the signature algorithms it takes, for the client's RSA key.
        send(Writer()
                 .byte(EXT_INFO)
                 .u32(1)
                 .string("server-sig-algs")
                 .string("ssh-ed25519,rsa-sha2-512,rsa-sha2-256")
                 .take());
    }

    void run() {
        out = "SSH-2.0-MockServer_1.0\r\n";
        flushOut();
        const auto until = Clock::now() + 10s;
        while (in.find("\r\n") == std::string::npos) {
            if (Clock::now() > until || server.quit_) throw failure("no version from the client");
            std::vector<net::Wait> waits{{socket->handle(), true, false}};
            net::waitAny(waits, 50);
            if (waits[0].can_read && socket->receive(in, 1) < 0) return;
        }
        v_c = in.substr(0, in.find("\r\n"));
        in.erase(0, v_c.size() + 2);
        kex();
        while (auto m = next()) {
            if (!handle(*m)) return;
            flushChannels();
            if (server.options_.cut_after > 0 && written_here >= server.options_.cut_after) {
                // The network cut: no goodbye.
                socket->shutdownBoth();
                return;
            }
        }
    }

    // --- Messages ---

    bool handle(const std::string& m) {
        Reader r(std::string_view(m).substr(1));
        switch (static_cast<uint8_t>(m[0])) {
            case DISCONNECT:
                return false;
            case IGNORE_DATA:
            case DEBUG_TEXT:
            case UNIMPLEMENTED:
                return true;
            case SERVICE_REQUEST:
                send(Writer().byte(SERVICE_ACCEPT).string(r.string()).take());
                return true;
            case USERAUTH_REQUEST:
                login(r);
                return true;
            case GLOBAL_REQUEST: {
                r.string();
                if (r.boolean()) send(Writer().byte(REQUEST_FAILURE).take());
                return true;
            }
            case CHANNEL_OPEN: {
                const auto type   = std::string(r.string());
                const auto sender = r.u32();
                const auto window = r.u32();
                const auto max    = r.u32();
                if (type != "session") {
                    send(Writer().byte(CHANNEL_OPEN_FAILURE).u32(sender).u32(3).string("no").string("").take());
                    return true;
                }
                const auto id = next_channel++;
                auto& c       = channels[id];
                c.peer        = sender;
                c.peer_window = window;
                c.peer_max    = max;
                send(Writer().byte(CHANNEL_OPEN_CONFIRMATION).u32(sender).u32(id).u32(1 << 21).u32(32768).take());
                return true;
            }
            case CHANNEL_REQUEST: {
                const auto id   = r.u32();
                const auto type = std::string(r.string());
                bool want_reply = r.boolean();
                auto it         = channels.find(id);
                if (it == channels.end()) throw failure("request on an unknown channel");
                auto& c = it->second;
                bool ok = false;
                if (type == "exec") {
                    const auto command = std::string(r.string());
                    {
                        std::lock_guard<std::mutex> lock(server.mutex_);
                        server.commands_.push_back(command);
                    }
                    MockServer::Command reply;
                    if (server.options_.exec) {
                        reply = server.options_.exec(command);
                    } else {
                        reply.err    = "sh: command not found\n";
                        reply.status = 127;
                    }
                    ok = true;
                    if (want_reply) send(Writer().byte(CHANNEL_SUCCESS).u32(c.peer).take());
                    want_reply = false;
                    c.out += reply.out;
                    c.err += reply.err;
                    c.status = reply.status;
                    if (reply.talk) {
                        c.kind = Channel::Kind::Talk;
                        c.talk = reply.talk;
                    } else {
                        c.kind     = Channel::Kind::Exec;
                        c.finished = true;
                    }
                } else if (type == "subsystem") {
                    ok = r.string() == "sftp";
                    if (ok) c.kind = Channel::Kind::Sftp;
                }
                if (want_reply) send(Writer().byte(ok ? CHANNEL_SUCCESS : CHANNEL_FAILURE).u32(c.peer).take());
                return true;
            }
            case CHANNEL_DATA: {
                const auto id   = r.u32();
                const auto data = std::string(r.string());
                auto it         = channels.find(id);
                if (it == channels.end()) return true;
                auto& c = it->second;
                send(Writer().byte(CHANNEL_WINDOW_ADJUST).u32(c.peer).u32(static_cast<uint32_t>(data.size())).take());
                if (c.kind == Channel::Kind::Talk && !c.finished) {
                    bool ended = false;
                    c.out += c.talk(data, false, ended);
                    c.finished = ended;
                } else if (c.kind == Channel::Kind::Sftp) {
                    c.sftp_in += data;
                    sftp(c);
                }
                return true;
            }
            case CHANNEL_EOF: {
                auto it = channels.find(r.u32());
                if (it != channels.end() && it->second.kind == Channel::Kind::Talk && !it->second.finished) {
                    bool ended = false;
                    it->second.out += it->second.talk({}, true, ended);
                    it->second.finished = true;
                }
                return true;
            }
            case CHANNEL_CLOSE: {
                const auto id = r.u32();
                auto it       = channels.find(id);
                if (it == channels.end()) return true;
                if (!it->second.closed_sent) send(Writer().byte(CHANNEL_CLOSE).u32(it->second.peer).take());
                channels.erase(it);
                return true;
            }
            case CHANNEL_WINDOW_ADJUST: {
                const auto id = r.u32();
                auto it       = channels.find(id);
                if (it != channels.end()) it->second.peer_window += r.u32();
                return true;
            }
            default:
                send(Writer().byte(UNIMPLEMENTED).u32(seq_rx - 1).take());
                return true;
        }
    }

    void login(Reader& r) {
        const auto user    = std::string(r.string());
        const auto service = std::string(r.string());
        const auto method  = std::string(r.string());
        const auto& keys   = server.options_.keys;
        const auto refuse  = [&] { send(Writer().byte(USERAUTH_FAILURE).string("publickey").boolean(false).take()); };
        if (user != server.options_.user) return refuse();
        if (method == "none") {
            if (!keys.empty()) return refuse();
            ++server.logins;
            send(Writer().byte(USERAUTH_SUCCESS).take());
            return;
        }
        if (method != "publickey") return refuse();
        const bool signed_ = r.boolean();
        const auto algo    = std::string(r.string());
        const auto blob    = std::string(r.string());
        if (std::find(keys.begin(), keys.end(), blob) == keys.end()) return refuse();
        if (!signed_) {
            send(Writer().byte(USERAUTH_PK_OK).string(algo).string(blob).take());
            return;
        }
        const auto signature = std::string(r.string());
        Reader s(signature);
        const auto sig_algo = std::string(s.string());
        const auto sig      = std::string(s.string());
        const auto data     = Writer()
                                  .string(session_id)
                                  .byte(USERAUTH_REQUEST)
                                  .string(user)
                                  .string(service)
                                  .string("publickey")
                                  .boolean(true)
                                  .string(algo)
                                  .string(blob)
                                  .take();
        bool ok             = false;
        if (algo == "ssh-ed25519" && sig_algo == algo) {
            ok = sig.size() == 64;
            if (ok) ++server.ed25519_logins;
        } else if ((algo == "rsa-sha2-256" || algo == "rsa-sha2-512") && sig_algo == algo) {
            ok = verifyRsa(blob, algo, data, sig);
            if (ok) ++server.rsa_logins;
        }
        if (!ok) return refuse();
        ++server.logins;
        send(Writer().byte(USERAUTH_SUCCESS).take());
    }

    // What waits for each channel, as far as the client's window goes; a command that ended is closed after.
    void flushChannels() {
        for (auto& [id, c] : channels) {
            for (int stream = 0; stream < 2; ++stream) {
                auto& data = stream == 0 ? c.out : c.err;
                while (!data.empty() && c.peer_window > 0) {
                    const auto n = static_cast<std::size_t>(
                        std::min<uint64_t>({data.size(), c.peer_window, static_cast<uint64_t>(c.peer_max) - 64}));
                    Writer w;
                    w.byte(stream == 0 ? CHANNEL_DATA : CHANNEL_EXTENDED_DATA).u32(c.peer);
                    if (stream == 1) w.u32(1);
                    w.string(std::string_view(data).substr(0, n));
                    send(w.take());
                    data.erase(0, n);
                    c.peer_window -= n;
                }
            }
            if (c.finished && c.out.empty() && c.err.empty() && !c.closed_sent) {
                send(Writer()
                         .byte(CHANNEL_REQUEST)
                         .u32(c.peer)
                         .string("exit-status")
                         .boolean(false)
                         .u32(static_cast<uint32_t>(c.status))
                         .take());
                send(Writer().byte(CHANNEL_EOF).u32(c.peer).take());
                send(Writer().byte(CHANNEL_CLOSE).u32(c.peer).take());
                c.closed_sent = true;
            }
        }
    }

    // --- SFTP ---

    fs::path local(std::string_view path) const {
        std::string p(path);
        while (!p.empty() && p.front() == '/') p.erase(0, 1);
        if (p.empty() || p == ".") return server.options_.root;
        return server.options_.root / fs::path(p);
    }

    static void attrs(Writer& w, const fs::path& path) {
        std::error_code ec;
        const auto status = fs::status(path, ec);
        uint32_t perms    = static_cast<uint32_t>(status.permissions()) & 0777;
        perms |= fs::is_directory(status) ? 040000 : 0100000;
        const uint64_t size = fs::is_regular_file(status) ? fs::file_size(path, ec) : 0;
        w.u32(1 | 4).u32(static_cast<uint32_t>(size >> 32)).u32(static_cast<uint32_t>(size)).u32(perms);
    }

    // Reads ATTRS, the permissions when they are given (0 otherwise).
    static uint32_t readAttrs(Reader& r) {
        const auto flags = r.u32();
        uint32_t perms   = 0;
        if (flags & 1) u64(r);
        if (flags & 2) {
            r.u32();
            r.u32();
        }
        if (flags & 4) perms = r.u32();
        if (flags & 8) {
            r.u32();
            r.u32();
        }
        if (flags & 0x80000000u) {
            const auto n = r.u32();
            for (uint32_t i = 0; i < n; ++i) {
                r.string();
                r.string();
            }
        }
        return perms;
    }

    void reply(Channel& c, const std::string& packet) {
        c.out += Writer().u32(static_cast<uint32_t>(packet.size())).take() + packet;
    }
    void status(Channel& c, uint32_t id, uint32_t code) {
        reply(c, Writer().byte(FXP_STATUS).u32(id).u32(code).string(code == FX_OK ? "ok" : "failed").string("").take());
    }

    void sftp(Channel& c) {
        while (c.sftp_in.size() >= 4) {
            Reader head(c.sftp_in);
            const auto length = head.u32();
            if (c.sftp_in.size() < 4 + std::size_t(length)) break;
            const auto packet = c.sftp_in.substr(4, length);
            c.sftp_in.erase(0, 4 + std::size_t(length));
            Reader r(packet);
            const auto type = r.byte();
            if (type == FXP_INIT) {
                reply(c, Writer().byte(FXP_VERSION).u32(3).take());
                continue;
            }
            const auto id = r.u32();
            sftpRequest(c, type, id, r);
        }
    }

    void sftpRequest(Channel& c, uint8_t type, uint32_t id, Reader& r) {
        std::error_code ec;
        switch (type) {
            case FXP_OPEN: {
                const auto path   = local(r.string());
                const auto pflags = r.u32();
                readAttrs(r);
                const bool write = (pflags & 2) != 0;
                if (!fs::exists(path, ec) && !(write && (pflags & 8))) return status(c, id, FX_NO_SUCH_FILE);
                if (write && ((pflags & 0x10) || !fs::exists(path, ec))) std::ofstream(path, std::ios::binary);
                auto file = std::make_unique<std::fstream>(
                    path, write ? std::ios::binary | std::ios::in | std::ios::out : std::ios::binary | std::ios::in);
                if (!*file) return status(c, id, FX_FAILURE);
                const auto handle = "h" + std::to_string(next_handle++);
                handles[handle]   = {path, std::move(file), {}, 0};
                reply(c, Writer().byte(FXP_HANDLE).u32(id).string(handle).take());
                return;
            }
            case FXP_CLOSE: {
                handles.erase(std::string(r.string()));
                return status(c, id, FX_OK);
            }
            case FXP_READ: {
                auto it = handles.find(std::string(r.string()));
                if (it == handles.end() || !it->second.file) return status(c, id, FX_FAILURE);
                const auto offset = u64(r);
                const auto size   = r.u32();
                auto& f           = *it->second.file;
                f.clear();
                f.seekg(static_cast<std::streamoff>(offset));
                std::string data(std::min<uint32_t>(size, 65536), '\0');
                f.read(data.data(), static_cast<std::streamsize>(data.size()));
                data.resize(static_cast<std::size_t>(f.gcount()));
                if (data.empty()) return status(c, id, FX_EOF);
                reply(c, Writer().byte(FXP_DATA).u32(id).string(data).take());
                return;
            }
            case FXP_WRITE: {
                auto it = handles.find(std::string(r.string()));
                if (it == handles.end() || !it->second.file) return status(c, id, FX_FAILURE);
                const auto offset = u64(r);
                const auto data   = r.string();
                auto& f           = *it->second.file;
                f.clear();
                f.seekp(static_cast<std::streamoff>(offset));
                f.write(data.data(), static_cast<std::streamsize>(data.size()));
                f.flush();
                written_here += data.size();
                server.written += data.size();
                return status(c, id, f ? FX_OK : FX_FAILURE);
            }
            case FXP_LSTAT:
            case FXP_STAT: {
                const auto path = local(r.string());
                if (!fs::exists(path, ec)) return status(c, id, FX_NO_SUCH_FILE);
                Writer w;
                w.byte(FXP_ATTRS).u32(id);
                attrs(w, path);
                reply(c, w.take());
                return;
            }
            case FXP_FSTAT: {
                auto it = handles.find(std::string(r.string()));
                if (it == handles.end()) return status(c, id, FX_FAILURE);
                Writer w;
                w.byte(FXP_ATTRS).u32(id);
                attrs(w, it->second.path);
                reply(c, w.take());
                return;
            }
            case FXP_SETSTAT: {
                const auto path  = local(r.string());
                const auto perms = readAttrs(r);
                if (!fs::exists(path, ec)) return status(c, id, FX_NO_SUCH_FILE);
                if (perms) fs::permissions(path, static_cast<fs::perms>(perms & 0777), ec);
                return status(c, id, FX_OK);
            }
            case FXP_FSETSTAT:
                return status(c, id, FX_OK);
            case FXP_OPENDIR: {
                const auto path = local(r.string());
                if (!fs::is_directory(path, ec)) return status(c, id, FX_NO_SUCH_FILE);
                Handle h;
                h.path = path;
                for (const auto& e : fs::directory_iterator(path, ec)) h.entries.push_back(e);
                const auto handle = "d" + std::to_string(next_handle++);
                handles[handle]   = std::move(h);
                reply(c, Writer().byte(FXP_HANDLE).u32(id).string(handle).take());
                return;
            }
            case FXP_READDIR: {
                auto it = handles.find(std::string(r.string()));
                if (it == handles.end()) return status(c, id, FX_FAILURE);
                auto& h = it->second;
                if (h.next >= h.entries.size()) return status(c, id, FX_EOF);
                const auto count = std::min<std::size_t>(20, h.entries.size() - h.next);
                Writer w;
                w.byte(FXP_NAME).u32(id).u32(static_cast<uint32_t>(count));
                for (std::size_t i = 0; i < count; ++i) {
                    const auto& e   = h.entries[h.next++];
                    const auto name = e.path().filename().string();
                    w.string(name).string(name);
                    attrs(w, e.path());
                }
                reply(c, w.take());
                return;
            }
            case FXP_REMOVE: {
                const auto path = local(r.string());
                if (!fs::exists(path, ec)) return status(c, id, FX_NO_SUCH_FILE);
                return status(c, id, fs::remove(path, ec) ? FX_OK : FX_FAILURE);
            }
            case FXP_MKDIR: {
                const auto path = local(r.string());
                readAttrs(r);
                return status(c, id, fs::create_directory(path, ec) ? FX_OK : FX_FAILURE);
            }
            case FXP_RMDIR: {
                const auto path = local(r.string());
                return status(c, id, fs::remove(path, ec) ? FX_OK : FX_FAILURE);
            }
            case FXP_REALPATH: {
                std::string path(r.string());
                if (path.empty() || path == ".") path = "/";
                if (path.front() != '/') path = "/" + path;
                reply(c, Writer().byte(FXP_NAME).u32(id).u32(1).string(path).string(path).u32(0).take());
                return;
            }
            case FXP_RENAME: {
                const auto from = local(r.string());
                const auto to   = local(r.string());
                // As OpenSSH's server: version 3 does not replace a file.
                if (!fs::exists(from, ec)) return status(c, id, FX_NO_SUCH_FILE);
                if (fs::exists(to, ec)) return status(c, id, FX_FAILURE);
                fs::rename(from, to, ec);
                return status(c, id, ec ? FX_FAILURE : FX_OK);
            }
            default:
                return status(c, id, FX_OP_UNSUPPORTED);
        }
    }
};

// ---------------------------------------------------------------------------------------------------------------

MockServer::MockServer(uint16_t port, Options options)
    : options_(std::move(options)), listener_(std::make_unique<Listener>()) {
    (void)hostKey();  // made before a client waits for it
    const auto error = listener_->tcp.listen(port);
    if (!error.empty()) throw failure("cannot listen: " + error);
    thread_ = std::thread([this] { serve(); });
}

MockServer::~MockServer() {
    quit_ = true;
    if (thread_.joinable()) thread_.join();
}

std::vector<std::string> MockServer::commands() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return commands_;
}

std::string MockServer::error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return error_;
}

void MockServer::serve() {
    while (!quit_) {
        const auto fd = listener_->tcp.accept(50);
        if (!fd) continue;
        ++connections;
        try {
            Connection connection(*this, *fd);
            connection.run();
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (error_.empty()) error_ = e.what();
        }
    }
}

}  // namespace fcg::ssh
