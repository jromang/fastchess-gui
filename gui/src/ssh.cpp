#include "ssh.hpp"

#include <algorithm>
#include <type_traits>

#include <mbedtls/platform_util.h>
#include <psa/crypto.h>

namespace fcg::ssh {

static_assert(std::is_same_v<mbedtls_svc_key_id_t, uint32_t>, "key ids are kept as uint32_t");

namespace {

void putU32(std::string& out, uint32_t v) {
    out += static_cast<char>(v >> 24);
    out += static_cast<char>(v >> 16);
    out += static_cast<char>(v >> 8);
    out += static_cast<char>(v);
}

uint32_t getU32(std::string_view s, std::size_t at = 0) {
    const auto b = [&](std::size_t i) { return static_cast<uint32_t>(static_cast<unsigned char>(s[at + i])); };
    return b(0) << 24 | b(1) << 16 | b(2) << 8 | b(3);
}

const uint8_t* bytes(std::string_view s) { return reinterpret_cast<const uint8_t*>(s.data()); }
uint8_t* bytes(std::string& s) { return reinterpret_cast<uint8_t*>(s.data()); }

// A key imported for one use, destroyed with the object.
class Key {
   public:
    Key() = default;
    ~Key() { reset(); }
    Key(const Key&)            = delete;
    Key& operator=(const Key&) = delete;

    bool import(psa_key_type_t type, psa_algorithm_t alg, psa_key_usage_t usage, std::string_view material) {
        reset();
        psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_type(&a, type);
        psa_set_key_algorithm(&a, alg);
        psa_set_key_usage_flags(&a, usage);
        const bool ok = psa_import_key(&a, bytes(material), material.size(), &id_) == PSA_SUCCESS;
        psa_reset_key_attributes(&a);
        if (!ok) id_ = 0;
        return ok;
    }
    void reset() {
        if (id_ != 0) psa_destroy_key(id_);
        id_ = 0;
    }
    [[nodiscard]] mbedtls_svc_key_id_t id() const noexcept { return id_; }

   private:
    mbedtls_svc_key_id_t id_ = 0;
};

// DER: a length, then an INTEGER and a SEQUENCE, enough for an RSAPublicKey (RFC 8017, A.1.1).
void derLength(std::string& out, std::size_t n) {
    if (n < 128) {
        out += static_cast<char>(n);
        return;
    }
    std::string digits;
    for (; n > 0; n >>= 8) digits.insert(digits.begin(), static_cast<char>(n & 255));
    out += static_cast<char>(0x80 | digits.size());
    out += digits;
}

// A positive integer from big-endian bytes, as DER wants it: no leading zeros but one before a high bit.
std::string derInteger(std::string_view big_endian) {
    while (!big_endian.empty() && big_endian.front() == 0) big_endian.remove_prefix(1);
    std::string value;
    if (big_endian.empty() || (static_cast<unsigned char>(big_endian.front()) & 0x80)) value += '\0';
    value += big_endian;
    std::string out = "\x02";
    derLength(out, value.size());
    return out + value;
}

}  // namespace

Writer& Writer::byte(uint8_t b) {
    out_ += static_cast<char>(b);
    return *this;
}

Writer& Writer::u32(uint32_t v) {
    putU32(out_, v);
    return *this;
}

Writer& Writer::string(std::string_view s) {
    putU32(out_, static_cast<uint32_t>(s.size()));
    out_ += s;
    return *this;
}

Writer& Writer::mpint(std::string_view n) {
    while (!n.empty() && n.front() == 0) n.remove_prefix(1);
    const bool high = !n.empty() && (static_cast<unsigned char>(n.front()) & 0x80);
    putU32(out_, static_cast<uint32_t>(n.size() + (high ? 1 : 0)));
    if (high) out_ += '\0';
    out_ += n;
    return *this;
}

Writer& Writer::raw(std::string_view s) {
    out_ += s;
    return *this;
}

uint8_t Reader::byte() {
    if (!ok_ || left() < 1) {
        ok_ = false;
        return 0;
    }
    return static_cast<uint8_t>(data_[at_++]);
}

uint32_t Reader::u32() {
    if (!ok_ || left() < 4) {
        ok_ = false;
        return 0;
    }
    const auto v = getU32(data_, at_);
    at_ += 4;
    return v;
}

std::string_view Reader::string(std::size_t max) {
    const auto n = u32();
    if (!ok_ || n > max || n > left()) {
        ok_ = false;
        return {};
    }
    const auto s = data_.substr(at_, n);
    at_ += n;
    return s;
}

std::string_view Reader::raw(std::size_t size) {
    if (!ok_ || size > left()) {
        ok_ = false;
        return {};
    }
    const auto s = data_.substr(at_, size);
    at_ += size;
    return s;
}

bool startCrypto() { return psa_crypto_init() == PSA_SUCCESS; }

void wipe(std::string& secret) {
    if (!secret.empty()) mbedtls_platform_zeroize(secret.data(), secret.size());
    secret.clear();
}

std::string sha256(std::string_view data) {
    std::string out(32, '\0');
    std::size_t n = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, bytes(data), data.size(), bytes(out), out.size(), &n) != PSA_SUCCESS) {
        return {};
    }
    return out;
}

std::string base64(std::string_view data, bool padding) {
    static constexpr char DIGITS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < data.size(); i += 3) {
        uint32_t n = static_cast<uint32_t>(static_cast<unsigned char>(data[i])) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(static_cast<unsigned char>(data[i + 1])) << 8;
        if (i + 2 < data.size()) n |= static_cast<unsigned char>(data[i + 2]);
        out += DIGITS[(n >> 18) & 63];
        out += DIGITS[(n >> 12) & 63];
        if (i + 1 < data.size()) {
            out += DIGITS[(n >> 6) & 63];
        } else if (padding) {
            out += '=';
        }
        if (i + 2 < data.size()) {
            out += DIGITS[n & 63];
        } else if (padding) {
            out += '=';
        }
    }
    return out;
}

std::string fingerprint(std::string_view key_blob) { return "SHA256:" + base64(sha256(key_blob), false); }

std::string randomBytes(std::size_t size) {
    std::string out(size, '\0');
    if (size > 0 && psa_generate_random(bytes(out), size) != PSA_SUCCESS) return {};
    return out;
}

std::string deriveKey(std::string_view k, std::string_view h, char letter, std::string_view session_id,
                      std::size_t size) {
    std::string base(k);
    base += h;
    std::string input = base + letter + std::string(session_id);
    auto out          = sha256(input);
    // K1 = HASH(K || H || X || session_id), then Kn = HASH(K || H || K1 || ... || Kn-1).
    while (out.size() < size) {
        wipe(input);
        input = base + out;
        out += sha256(input);
    }
    wipe(input);
    wipe(base);
    if (out.size() > size) mbedtls_platform_zeroize(out.data() + size, out.size() - size);
    out.resize(size);
    return out;
}

X25519::X25519(std::string_view private_key) {
    if (!startCrypto()) return;
    psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&a, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
    psa_set_key_bits(&a, 255);
    psa_set_key_algorithm(&a, PSA_ALG_ECDH);
    psa_set_key_usage_flags(&a, PSA_KEY_USAGE_DERIVE);
    mbedtls_svc_key_id_t id = 0;
    const auto status       = private_key.empty() ? psa_generate_key(&a, &id)
                                                  : psa_import_key(&a, bytes(private_key), private_key.size(), &id);
    psa_reset_key_attributes(&a);
    if (status != PSA_SUCCESS) return;
    std::string pub(32, '\0');
    std::size_t n = 0;
    if (psa_export_public_key(id, bytes(pub), pub.size(), &n) != PSA_SUCCESS || n != 32) {
        psa_destroy_key(id);
        return;
    }
    key_    = id;
    public_ = std::move(pub);
}

X25519::~X25519() {
    if (key_ != 0) psa_destroy_key(key_);
}

std::optional<std::string> X25519::agree(std::string_view peer) const {
    if (key_ == 0 || peer.size() != 32) return std::nullopt;
    std::string secret(32, '\0');
    std::size_t n = 0;
    if (psa_raw_key_agreement(PSA_ALG_ECDH, key_, bytes(peer), peer.size(), bytes(secret), secret.size(), &n) !=
            PSA_SUCCESS ||
        n != 32) {
        return std::nullopt;
    }
    // A peer key of small order gives zero: refused (RFC 7748, 6.1).
    if (std::all_of(secret.begin(), secret.end(), [](char c) { return c == 0; })) return std::nullopt;
    return secret;
}

std::string verifyRsa(std::string_view key_blob, std::string_view signature, std::string_view data,
                      std::string_view algorithm) {
    Reader key(key_blob);
    const auto type = key.string(64);
    const auto e    = key.string(1024);
    const auto n    = key.string(2048);
    if (!key.ok() || type != "ssh-rsa") return "the host key is not an RSA key";
    // Positive numbers: no high bit in the first byte.
    if (e.empty() || n.empty() || (static_cast<unsigned char>(e[0]) & 0x80) ||
        (static_cast<unsigned char>(n[0]) & 0x80)) {
        return "the host key is not a valid RSA key";
    }
    auto modulus = n;
    while (!modulus.empty() && modulus.front() == 0) modulus.remove_prefix(1);
    const auto bits = modulus.size() * 8;
    if (bits < 2048 || bits > 16384) return "the host key's size is not accepted (2048 to 16384 bits)";

    Reader sig(signature);
    const auto name = sig.string(64);
    auto value      = std::string(sig.string(2048));
    if (!sig.ok() || sig.left() != 0) return "the host key's signature cannot be read";
    if (!algorithm.empty() && name != algorithm) return "the host key's signature is not of the algorithm agreed";
    psa_algorithm_t hash = 0;
    if (name == "rsa-sha2-512") {
        hash = PSA_ALG_SHA_512;
    } else if (name == "rsa-sha2-256") {
        hash = PSA_ALG_SHA_256;
    } else {
        return "the host key's signature algorithm is not accepted";
    }
    // Some servers drop the signature's leading zeros: it is as long as the modulus.
    if (value.size() > modulus.size()) return "the host key's signature is not valid";
    value.insert(0, modulus.size() - value.size(), '\0');

    std::string body = derInteger(n) + derInteger(e);
    std::string der  = "\x30";
    derLength(der, body.size());
    der += body;
    if (!startCrypto()) return "the cryptography cannot start";
    const auto alg = PSA_ALG_RSA_PKCS1V15_SIGN(hash);
    Key rsa;
    if (!rsa.import(PSA_KEY_TYPE_RSA_PUBLIC_KEY, alg, PSA_KEY_USAGE_VERIFY_MESSAGE, der)) {
        return "the host key cannot be used";
    }
    if (psa_verify_message(rsa.id(), alg, bytes(data), data.size(), bytes(value), value.size()) != PSA_SUCCESS) {
        return "the host key's signature is not valid";
    }
    return {};
}

// ---------------------------------------------------------------------------------------------------------------
// Packets

namespace {

enum class Mode { Clear, Gcm, Ctr, CtrEtm };

constexpr std::size_t GCM_TAG = 16;
constexpr std::size_t HMAC_SIZE = 32;  // hmac-sha2-256

}  // namespace

struct PacketCipher::State {
    Mode mode = Mode::Clear;
    Key cipher;
    Key mac;
    std::string iv;  // GCM: the fixed part and the invocation counter (RFC 5647, 7.1)
    psa_cipher_operation_t ctr = PSA_CIPHER_OPERATION_INIT;
    bool ctr_open              = false;
    // CTR without encrypt-then-MAC: the first block, already deciphered to read the length.
    std::string first;

    ~State() { closeCtr(); }
    void closeCtr() {
        if (ctr_open) psa_cipher_abort(&ctr);
        ctr      = PSA_CIPHER_OPERATION_INIT;
        ctr_open = false;
    }
    [[nodiscard]] std::size_t block() const { return mode == Mode::Clear ? 8 : 16; }
    [[nodiscard]] std::size_t tag() const {
        return mode == Mode::Gcm ? GCM_TAG : mode == Mode::Clear ? 0 : HMAC_SIZE;
    }
    // CTR keeps its counter from one packet to the next: one operation for the connection.
    bool crypt(std::string_view in, std::string& out) {
        out.resize(in.size());
        std::size_t n = 0;
        return psa_cipher_update(&ctr, bytes(in), in.size(), bytes(out), out.size(), &n) == PSA_SUCCESS &&
               n == in.size();
    }
    std::string hmac(std::string_view data) {
        std::string out(HMAC_SIZE, '\0');
        std::size_t n = 0;
        if (psa_mac_compute(mac.id(), PSA_ALG_HMAC(PSA_ALG_SHA_256), bytes(data), data.size(), bytes(out), out.size(),
                            &n) != PSA_SUCCESS) {
            return std::string(HMAC_SIZE, '\0');
        }
        return out;
    }
    // Constant time, through PSA.
    bool hmacValid(std::string_view data, std::string_view tag) {
        return psa_mac_verify(mac.id(), PSA_ALG_HMAC(PSA_ALG_SHA_256), bytes(data), data.size(), bytes(tag),
                              tag.size()) == PSA_SUCCESS;
    }
    // The invocation counter, the last 8 bytes of the nonce, counts the packets.
    void nextIv() {
        for (std::size_t i = iv.size(); i-- > 4;) {
            if (++reinterpret_cast<unsigned char&>(iv[i]) != 0) break;
        }
    }
};

PacketCipher::PacketCipher() : s_(std::make_unique<State>()) {}
PacketCipher::~PacketCipher() = default;

bool PacketCipher::knownCipher(std::string_view c) {
    return c == "aes256-gcm@openssh.com" || c == "aes128-gcm@openssh.com" || c == "aes256-ctr" || c == "aes128-ctr";
}
bool PacketCipher::knownMac(std::string_view m) { return m == "hmac-sha2-256-etm@openssh.com" || m == "hmac-sha2-256"; }
bool PacketCipher::aead(std::string_view c) { return c.find("-gcm@") != std::string_view::npos; }
std::size_t PacketCipher::keySize(std::string_view c) { return c.rfind("aes128", 0) == 0 ? 16 : 32; }
std::size_t PacketCipher::ivSize(std::string_view c) { return aead(c) ? 12 : 16; }
std::size_t PacketCipher::macKeySize(std::string_view m) { return knownMac(m) ? 32 : 0; }

std::string PacketCipher::start(std::string_view cipher, std::string_view mac, std::string_view key,
                                std::string_view iv, std::string_view mac_key) {
    if (!startCrypto()) return "the cryptography cannot start";
    if (!knownCipher(cipher)) return "unknown cipher";
    if (key.size() != keySize(cipher) || iv.size() != ivSize(cipher)) return "bad key size";
    auto next = std::make_unique<State>();
    if (aead(cipher)) {
        next->mode = Mode::Gcm;
        if (!next->cipher.import(PSA_KEY_TYPE_AES, PSA_ALG_GCM, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT, key)) {
            return "the cipher key cannot be used";
        }
        next->iv = std::string(iv);
    } else {
        if (!knownMac(mac) || mac_key.size() != macKeySize(mac)) return "unknown MAC";
        next->mode = mac.find("-etm@") != std::string_view::npos ? Mode::CtrEtm : Mode::Ctr;
        if (!next->cipher.import(PSA_KEY_TYPE_AES, PSA_ALG_CTR, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT, key) ||
            !next->mac.import(PSA_KEY_TYPE_HMAC, PSA_ALG_HMAC(PSA_ALG_SHA_256),
                              PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE, mac_key)) {
            return "the keys cannot be used";
        }
        // CTR is its own inverse: an encryption operation serves both directions' use of it.
        if (psa_cipher_encrypt_setup(&next->ctr, next->cipher.id(), PSA_ALG_CTR) != PSA_SUCCESS) {
            return "the cipher cannot start";
        }
        next->ctr_open = true;
        if (psa_cipher_set_iv(&next->ctr, bytes(iv), iv.size()) != PSA_SUCCESS) return "the cipher cannot start";
    }
    s_ = std::move(next);
    return {};
}

std::string PacketCipher::seal(std::string_view payload, uint32_t seq) {
    auto& s          = *s_;
    const auto block = s.block();
    // The length is outside what is padded with GCM and encrypt-then-MAC: it is not encrypted.
    const bool length_apart = s.mode == Mode::Gcm || s.mode == Mode::CtrEtm;
    const auto covered      = (length_apart ? 0 : 4) + 1 + payload.size();
    auto padding            = block - covered % block;
    if (padding < 4) padding += block;
    // 16 bytes at least, the length included.
    while (4 + 1 + payload.size() + padding < 16) padding += block;
    const auto length = static_cast<uint32_t>(1 + payload.size() + padding);

    std::string body;
    body.reserve(length);
    body += static_cast<char>(padding);
    body += payload;
    body += randomBytes(padding);
    if (body.size() != length) body.resize(length, '\0');

    std::string out;
    putU32(out, length);
    switch (s.mode) {
        case Mode::Clear:
            out += body;
            break;
        case Mode::Gcm: {
            std::string sealed(body.size() + GCM_TAG, '\0');
            std::size_t n = 0;
            psa_aead_encrypt(s.cipher.id(), PSA_ALG_GCM, bytes(s.iv), s.iv.size(), bytes(out), 4, bytes(body),
                             body.size(), bytes(sealed), sealed.size(), &n);
            s.nextIv();
            out += sealed;
            break;
        }
        case Mode::Ctr: {
            std::string mac_input;
            putU32(mac_input, seq);
            mac_input += out;
            mac_input += body;
            const auto tag = s.hmac(mac_input);
            std::string clear = out + body, sealed;
            s.crypt(clear, sealed);
            out = sealed + tag;
            break;
        }
        case Mode::CtrEtm: {
            std::string sealed;
            s.crypt(body, sealed);
            std::string mac_input;
            putU32(mac_input, seq);
            mac_input += out;
            mac_input += sealed;
            out += sealed;
            out += s.hmac(mac_input);
            break;
        }
    }
    return out;
}

PacketCipher::Open PacketCipher::open(std::string& in, uint32_t seq, std::string& payload) {
    auto& s          = *s_;
    const auto block = s.block();
    const auto tag   = s.tag();

    // The length: in the clear, or in the first block (CTR without encrypt-then-MAC).
    std::size_t length = 0;
    if (s.mode == Mode::Ctr) {
        if (s.first.empty()) {
            if (in.size() < block) return Open::More;
            if (!s.crypt(std::string_view(in).substr(0, block), s.first)) return Open::Bad;
        }
        length = getU32(s.first);
    } else {
        if (in.size() < 4) return Open::More;
        length = getU32(in);
    }
    // Bounded before anything waits for it, and whole blocks. All in size_t: a length near 2^32 must not wrap to a
    // small total.
    const bool length_apart = s.mode == Mode::Gcm || s.mode == Mode::CtrEtm;
    if (length < 5 || length > MAX_PACKET - 4 || (length + (length_apart ? 0 : 4)) % block != 0) return Open::Bad;
    if (s.mode == Mode::Ctr && 4 + length < block) return Open::Bad;
    const std::size_t total = 4 + length + tag;
    if (in.size() < total) return Open::More;

    const std::string_view packet(in.data(), total);
    std::string body;  // padding length, payload, padding
    switch (s.mode) {
        case Mode::Clear:
            body = std::string(packet.substr(4, length));
            break;
        case Mode::Gcm: {
            body.resize(length);
            std::size_t n = 0;
            if (psa_aead_decrypt(s.cipher.id(), PSA_ALG_GCM, bytes(s.iv), s.iv.size(), bytes(packet), 4,
                                 bytes(packet) + 4, length + GCM_TAG, bytes(body), body.size(),
                                 &n) != PSA_SUCCESS ||
                n != length) {
                return Open::Bad;
            }
            s.nextIv();
            break;
        }
        case Mode::Ctr: {
            std::string rest;
            if (!s.crypt(packet.substr(block, 4 + length - block), rest)) return Open::Bad;
            std::string mac_input;
            putU32(mac_input, seq);
            mac_input += s.first;
            mac_input += rest;
            s.first.clear();
            if (!s.hmacValid(mac_input, packet.substr(4 + length, tag))) return Open::Bad;
            body = mac_input.substr(8);
            break;
        }
        case Mode::CtrEtm: {
            std::string mac_input;
            putU32(mac_input, seq);
            mac_input += packet.substr(0, 4 + length);
            if (!s.hmacValid(mac_input, packet.substr(4 + length, tag))) return Open::Bad;
            if (!s.crypt(packet.substr(4, length), body)) return Open::Bad;
            break;
        }
    }
    if (body.size() != length) return Open::Bad;
    const std::size_t padding = static_cast<unsigned char>(body[0]);
    if (padding < 4 || padding + 1 > length) return Open::Bad;
    payload = body.substr(1, length - 1 - padding);
    in.erase(0, total);
    return Open::Packet;
}

}  // namespace fcg::ssh
