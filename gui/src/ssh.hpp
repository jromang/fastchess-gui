#pragma once

// The SSH protocol (RFC 4251 to 4254) as far as a remote forward needs it, on the PSA cryptography of the Mbed TLS
// the program ships: the curve25519-sha256 key exchange, RSA host keys (rsa-sha2-512, rsa-sha2-256), AES-GCM or
// AES-CTR with HMAC-SHA2-256 for the packets. The tunnel itself is ssh_tunnel.cpp; these are its pieces, testable on
// their own.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace fcg::ssh {

// The largest packet taken from the network (RFC 4253, 6.1): anything larger ends the connection.
constexpr std::size_t MAX_PACKET = 35000;

enum Message : uint8_t {
    DISCONNECT                = 1,
    IGNORE                    = 2,
    UNIMPLEMENTED             = 3,
    DEBUG                     = 4,
    SERVICE_REQUEST           = 5,
    SERVICE_ACCEPT            = 6,
    EXT_INFO                  = 7,
    KEXINIT                   = 20,
    NEWKEYS                   = 21,
    KEX_ECDH_INIT             = 30,
    KEX_ECDH_REPLY            = 31,
    USERAUTH_REQUEST          = 50,
    USERAUTH_FAILURE          = 51,
    USERAUTH_SUCCESS          = 52,
    USERAUTH_BANNER           = 53,
    GLOBAL_REQUEST            = 80,
    REQUEST_SUCCESS           = 81,
    REQUEST_FAILURE           = 82,
    CHANNEL_OPEN              = 90,
    CHANNEL_OPEN_CONFIRMATION = 91,
    CHANNEL_OPEN_FAILURE      = 92,
    CHANNEL_WINDOW_ADJUST     = 93,
    CHANNEL_DATA              = 94,
    CHANNEL_EXTENDED_DATA     = 95,
    CHANNEL_EOF               = 96,
    CHANNEL_CLOSE             = 97,
    CHANNEL_REQUEST           = 98,
    CHANNEL_SUCCESS           = 99,
    CHANNEL_FAILURE           = 100,
};

// A message being written, in SSH's encodings (RFC 4251, 5).
class Writer {
   public:
    Writer& byte(uint8_t b);
    Writer& boolean(bool b) { return byte(b ? 1 : 0); }
    Writer& u32(uint32_t v);
    Writer& string(std::string_view s);
    // An unsigned big-endian number, as an mpint.
    Writer& mpint(std::string_view unsigned_big_endian);
    Writer& raw(std::string_view s);
    [[nodiscard]] const std::string& data() const noexcept { return out_; }
    [[nodiscard]] std::string take() { return std::move(out_); }

   private:
    std::string out_;
};

// A message being read. Every read is bounded by what is left; one that does not fit fails the reader (ok() false)
// and gives zeros or nothing from then on, so a message is checked once, at the end.
class Reader {
   public:
    explicit Reader(std::string_view data) : data_(data) {}
    uint8_t byte();
    bool boolean() { return byte() != 0; }
    uint32_t u32();
    // A string of `max` bytes at most.
    std::string_view string(std::size_t max = MAX_PACKET);
    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] std::size_t left() const noexcept { return data_.size() - at_; }

   private:
    std::string_view data_;
    std::size_t at_ = 0;
    bool ok_        = true;
};

// Starts the cryptography (once, any thread): false when it cannot.
bool startCrypto();

[[nodiscard]] std::string sha256(std::string_view data);
[[nodiscard]] std::string base64(std::string_view data, bool padding = true);
// "SHA256:..." as ssh-keygen -l shows it, of a host key's blob.
[[nodiscard]] std::string fingerprint(std::string_view key_blob);
// `size` bytes of cryptographically random data.
[[nodiscard]] std::string randomBytes(std::size_t size);

// A key of the exchange (RFC 4253, 7.2): HASH(K || H || letter || session_id), extended as needed. `k` is the shared
// secret encoded as an mpint, its length included.
[[nodiscard]] std::string deriveKey(std::string_view k, std::string_view h, char letter, std::string_view session_id,
                                    std::size_t size);

// An X25519 key pair for one key exchange (RFC 7748, through PSA ECDH on Montgomery 255).
class X25519 {
   public:
    // A new random key; or `private_key` (32 bytes) for the tests.
    explicit X25519(std::string_view private_key = {});
    ~X25519();
    X25519(const X25519&)            = delete;
    X25519& operator=(const X25519&) = delete;

    [[nodiscard]] bool ok() const noexcept { return key_ != 0; }
    [[nodiscard]] const std::string& publicKey() const noexcept { return public_; }
    // The shared secret with `peer`'s public key; nullopt when it is not one, or gives the all-zero secret.
    [[nodiscard]] std::optional<std::string> agree(std::string_view peer) const;

   private:
    uint32_t key_ = 0;  // psa_key_id_t
    std::string public_;
};

// Checks the signature an RSA host key made of `data` (the exchange hash): `key_blob` is "ssh-rsa" e n,
// `signature` is the algorithm's name and the signature. Empty when it is good, else why it is not. `algorithm`,
// when given, is the one the signature must be made with.
[[nodiscard]] std::string verifyRsa(std::string_view key_blob, std::string_view signature, std::string_view data,
                                    std::string_view algorithm = {});

// One direction of the packet layer (RFC 4253, 6): the packets sealed or opened with the keys of the last exchange,
// or in the clear before the first.
class PacketCipher {
   public:
    PacketCipher();
    ~PacketCipher();
    PacketCipher(const PacketCipher&)            = delete;
    PacketCipher& operator=(const PacketCipher&) = delete;

    // The algorithms supported, as SSH names them.
    [[nodiscard]] static bool knownCipher(std::string_view cipher);
    [[nodiscard]] static bool knownMac(std::string_view mac);
    [[nodiscard]] static bool aead(std::string_view cipher);  // no separate MAC
    [[nodiscard]] static std::size_t keySize(std::string_view cipher);
    [[nodiscard]] static std::size_t ivSize(std::string_view cipher);
    [[nodiscard]] static std::size_t macKeySize(std::string_view mac);

    // Uses `cipher` (and `mac`, unless it is an AEAD cipher) from now on. Empty on success, else why not.
    std::string start(std::string_view cipher, std::string_view mac, std::string_view key, std::string_view iv,
                      std::string_view mac_key);

    // The packet of `payload`, as sent, with sequence number `seq`.
    [[nodiscard]] std::string seal(std::string_view payload, uint32_t seq);

    enum class Open {
        More,    // not a whole packet yet
        Packet,  // one, taken from the front of `in`, in `payload`
        Bad,     // not a packet, or not authentic: the connection must end
    };
    // Takes the first packet from `in` when it is all there.
    Open open(std::string& in, uint32_t seq, std::string& payload);

   private:
    struct State;
    std::unique_ptr<State> s_;
};

}  // namespace fcg::ssh
