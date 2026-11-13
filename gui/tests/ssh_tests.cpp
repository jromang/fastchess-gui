#include <doctest/doctest.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <vector>
#include <string>
#include <thread>

#include <psa/crypto.h>

#include "broadcast.hpp"
#include "broadcaster.hpp"
#include "qr.hpp"
#include "net.hpp"
#include "ssh.hpp"
#include "ssh_tunnel.hpp"

using namespace fcg;
using namespace fcg::ssh;
using namespace std::chrono_literals;

namespace {

std::string unhex(std::string_view hex) {
    std::string out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out += static_cast<char>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16));
    }
    return out;
}

bool netTests() {
    const char* wanted = std::getenv("FCG_NET_TESTS");
    return wanted && std::string(wanted) == "1";
}

// Known answers, computed with Python's hashlib, hmac and cryptography (an RSA key made for the tests only), and
// localhost.run's public host key as ssh-keyscan fetched it.
const std::string KAT_K = unhex(
    "00000021008011111111111111111111111111111111111111111111111111111111111111");

const std::string KAT_H = unhex(
    "7bde03b6174b06ac22d902ada587e02f46b15e82e96fcc34d951b86efe0642f8");

const std::string KAT_SID = unhex(
    "1cc1c70c03d3fa98125ac304150c6b2ab44b1a308f55aab9e46e6de9f92ae871");

const std::string KAT_DERIVE_C = unhex(
    "4abda8f388b2a9d7aabc69c8372f3088797169604fb3b9501f756243514edaa3ba282e36d89ad2cb095f98076d694ac0"
    "764e4cd4cd740fb74f9052cf37004214");

const std::string KAT_DERIVE_A = unhex(
    "861f1a722c3916e7234823e1");

const std::string KAT_CTR = unhex(
    "227080461176627330130602d2df27fc15144438e3686d36d131b19aaf6af8bff47fab67a67e1397fe2c820a088d273d"
    "357e8248b6c0581f8a06a40dbfd256dcaed4eedfb2d1b8df08eb721a1c757da6b1bd78adddf8c5700aea68f4c5a6c8f9"
    "effaaf047aa78c48c1920b638d08b81f099f4625bd3b6a2859d8fbfc3e2669046e2504e98a3e4bc4575e1c5bad055269"
    "dfd97c304ad2ca7a0d410573046322ab");

const std::string KAT_ETM = unhex(
    "000000402b2ee81f7844653a7c0b1c409cce3e927b712838e3686d36d131b19aaf6af8bff47fab67a67e1397fe2c820a"
    "088d273d357e8248b6c058b520ac0e0dbfd256dc0c1d68e923c7639469fa432b59c31812125b584df66784de7f5e3282"
    "ca9a7d4300000020fea4dc7d14969149828d0464c61dadd6c8509825bd3b6a2859d8fbfc3e2669045764841f0e130716"
    "c6b7634de6ce4ec6848b55a7b867a9b857295adfef5d9aaa");

const std::string KAT_GCM = unhex(
    "000000403ddd7b5ee77e46cf712301aea3b525dd54b5b596045f2f2bcc1e3d16c55da015d6cf0de43b65cebfdb0bb981"
    "ef0eeb9b46a96a35fcf0e1138fa6021f3858358aac38cb6f7e68af309b0630a24c46c8ca000000203f23bb6fab8608e1"
    "14cbea18289afe0c3dc55955bf0e4dcc27b21f6843f065a19cdc41c263419fe461ae8791325eb90e");

const std::string KAT_RSA_BLOB = unhex(
    "000000077373682d727361000000030100010000010100946995e1263245680d8fccad0cc7b412801fe640665abfa8d9"
    "2076afe8ca9b23501a2874e4040365d6b9b62d6289640d4d7f508c36de4df88a931bf6599dc057cd8c1971d016081901"
    "a81bacca621e52702e93bfe70d7ca565462e5e2e38d54fe7ac28755b9370ac7fd192c24c3ddfada8e944475b771421b9"
    "5e751d697c8e908689114681b8ab44be69e340bba94cdffcf300caf00bc1bd51ef847524a7bd963343c6a3f87df849d5"
    "81bfd93e8ff17b4b39e0ad56154f8de322110339d87ce501fce598d42f2223f1ca575ad50a7a088e8929fbeb1d06eacd"
    "e40f04a87725716b2af86006eda894f270c58b67faf7d4d4558783e3cb44858abe494a3431bc07");

const std::string KAT_RSA_DATA = unhex(
    "20136e16e4afb1e5cfe4f7b118b038206ed725739f31e5bf84ba2be8b27710f4");

const std::string KAT_RSA_SIG512 = unhex(
    "0000000c7273612d736861322d3531320000010000b84b3211f7cd3fedf6b219acf1b86a5eb077cc26d494e5a072bfeb"
    "da3205f2c07f2dd999426d0bf38f88b6dfcf56b2c2687aab7e15e37ca29d3d36693254b21cd288ecc821b17cf3a19936"
    "8a9afd1fa52653ac62af8063bd6f9c2c861fe71c466fd04bb08b8d895859e79519212911d36145232f94544a85c802b7"
    "ec656d3799c873a6250047acc3443ac990ce33cdd82a679fb0dc0a332878fd54f160876f73c451d661fdd6ecdd90ac42"
    "2b94c12eca89b28f78bb6ade1080604fa7c810d74c74299d7263448fe2061f0d450cc7ece13d0adcb035885e52101ba5"
    "4ce9fcc41de9f9fb00ff31a33c0ba1a957d3dc7fe0f043b62c483a9c10144b39b3584242");

const std::string KAT_RSA_SIG256 = unhex(
    "0000000c7273612d736861322d323536000001002ebe0752657ea1596d43ba3076928cef81c3314f069d13f54f04223c"
    "6b7890f135319fec4964ffe0f09504231da696443a2e1ab1787ba4526b39c8463e33c3170d6aece776b7787fb684d0fa"
    "c0c14f8aa39faef25081a9c06fe40d512118501fe725765ba6987395e80bb9de5626bb343eb31558a02f5f743074a892"
    "2c39dc2eb5b6c109528d43fa7638c50eaf6f9affd6cd0edd09a9120e4ec5634620a29a2621c8b2aec79da77c579300da"
    "ebcf1e627574e497b559e0fd08436280c397e0ad36a46dbe385bb00237f882d0f07e6b7461498a6fcce461b6ad64f48e"
    "da95935ec3c1059e4ce66767f0db4b95da6f9cd433c174ece746083baaa8bbcaa94dc82e");

const std::string KAT_RSA_FP = "SHA256:jN/EOmDgRjYr0pNiIkwR5qK2u8HDUGLu53M2Z1Kv4kE";

const std::string LOCALHOST_RUN_BLOB = unhex(
    "000000077373682d727361000000030100010000010100b79499e15b5a025ee01857d20175c240f95c7b0072f94bf650"
    "bd5d9f85c78e00f80eda43506656bac6f530a04e22c3c958bb7ce81352ad89e094f7222758e548e96ff015a4ff1131f5"
    "b67e794f6155b0aff36b13e21d954918b3cf744122774bd2da9d490df73da27674a4d4872e5d50bac20e78c5326766d4"
    "30c2cb830e3a28e4fd4b7b3f2e6c60a09dcfa1c5549f9ad55f3fc39e0ed8f2360d7b8205ad938052c8bb84d05af8e623"
    "6ba71e79fa43bcd0c427505d85b61f1a894174d03b7fe14d97491f696aeee026e5afce37c017b67243bfb0daa0780317"
    "3bfa87f924a0431517176020030f931a9df2088c98a0e3ef3a0bdc3ec4338892e60db52e4299c7");

}  // namespace

TEST_CASE("SSH: the encodings, and reads past the end refused") {
    const auto m = Writer().byte(7).u32(0x01020304).string("abc").boolean(true).take();
    CHECK(m == std::string("\x07\x01\x02\x03\x04\x00\x00\x00\x03" "abc\x01", 13));
    Reader r(m);
    CHECK(r.byte() == 7);
    CHECK(r.u32() == 0x01020304);
    CHECK(r.string() == "abc");
    CHECK(r.boolean());
    CHECK(r.ok());
    r.u32();
    CHECK_FALSE(r.ok());

    // A string longer than the message, or than its bound.
    Reader longer(std::string("\x00\x00\x00\x09" "abc", 7));
    CHECK(longer.string().empty());
    CHECK_FALSE(longer.ok());
    Reader bounded(Writer().string("abcdef").data());
    bounded.string(5);
    CHECK_FALSE(bounded.ok());

    // mpint: no leading zeros, one before a high bit (RFC 4251, 5).
    CHECK(Writer().mpint(std::string("\x00\x00\x12", 3)).data() == std::string("\x00\x00\x00\x01\x12", 5));
    CHECK(Writer().mpint("\x80").data() == std::string("\x00\x00\x00\x02\x00\x80", 6));
    CHECK(Writer().mpint(std::string(2, '\0')).data() == std::string(4, '\0'));
}

TEST_CASE("SSH: key derivation as RFC 4253 7.2 says, extended past one hash") {
    CHECK(deriveKey(KAT_K, KAT_H, 'C', KAT_SID, 64) == KAT_DERIVE_C);
    CHECK(deriveKey(KAT_K, KAT_H, 'A', KAT_SID, 12) == KAT_DERIVE_A);
    CHECK(deriveKey(KAT_K, KAT_H, 'C', KAT_SID, 32) == KAT_DERIVE_C.substr(0, 32));
}

TEST_CASE("SSH: X25519 through PSA gives RFC 7748's shared secret, and refuses a small-order key") {
    const auto alice_private = unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    const auto bob_private   = unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    X25519 alice(alice_private), bob(bob_private);
    REQUIRE(alice.ok());
    REQUIRE(bob.ok());
    CHECK(alice.publicKey() == unhex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"));
    CHECK(bob.publicKey() == unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"));
    const auto shared = unhex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    CHECK(alice.agree(bob.publicKey()) == shared);
    CHECK(bob.agree(alice.publicKey()) == shared);
    // Zero is a point of small order: the secret would be zero.
    CHECK_FALSE(alice.agree(std::string(32, '\0')).has_value());
    CHECK_FALSE(alice.agree("short").has_value());
    // Random keys agree too.
    X25519 a, b;
    REQUIRE(a.ok());
    CHECK(a.agree(b.publicKey()) == b.agree(a.publicKey()));
}

TEST_CASE("SSH: RSA host key signatures, rsa-sha2-512 and rsa-sha2-256, and fingerprints") {
    CHECK(verifyRsa(KAT_RSA_BLOB, KAT_RSA_SIG512, KAT_RSA_DATA).empty());
    CHECK(verifyRsa(KAT_RSA_BLOB, KAT_RSA_SIG256, KAT_RSA_DATA, "rsa-sha2-256").empty());
    // The algorithm agreed must be the one used.
    CHECK_FALSE(verifyRsa(KAT_RSA_BLOB, KAT_RSA_SIG256, KAT_RSA_DATA, "rsa-sha2-512").empty());
    // Other data, or a signature changed by a bit.
    auto data = KAT_RSA_DATA;
    data[0] ^= 1;
    CHECK_FALSE(verifyRsa(KAT_RSA_BLOB, KAT_RSA_SIG512, data).empty());
    auto sig = KAT_RSA_SIG512;
    sig[sig.size() - 1] ^= 1;
    CHECK_FALSE(verifyRsa(KAT_RSA_BLOB, sig, KAT_RSA_DATA).empty());
    // Not an RSA key, or a truncated one.
    CHECK_FALSE(verifyRsa(Writer().string("ssh-ed25519").string(std::string(32, 'x')).data(), KAT_RSA_SIG512,
                          KAT_RSA_DATA)
                    .empty());
    CHECK_FALSE(verifyRsa(KAT_RSA_BLOB.substr(0, 40), KAT_RSA_SIG512, KAT_RSA_DATA).empty());

    CHECK(fingerprint(KAT_RSA_BLOB) == KAT_RSA_FP);
    // As ssh-keygen -l shows localhost.run's key, and as pinned.
    CHECK(fingerprint(LOCALHOST_RUN_BLOB) == "SHA256:FV8IMJ4IYjYUTnd6on7PqbRjaZf4c1EhhEBgeUdE94I");
    CHECK(localhostRun(8080).pinned == fingerprint(LOCALHOST_RUN_BLOB));
}

namespace {

const std::string PACKET_KEY     = unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
const std::string PACKET_MAC_KEY = unhex("202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f");
// The low half of the counter about to carry, as is the GCM invocation counter.
const std::string PACKET_IV16 = unhex("0001020304050607ffffffffffffffff");
const std::string PACKET_IV12 = unhex("0a0b0c0d00000000ffffffff");
const std::string PAYLOAD_1   = std::string("\x5ehello, tunnel") + std::string(40, '\0');
const std::string PAYLOAD_2   = "\x5esecond packet";

// Opens `wire`'s two packets, given in pieces of `step` bytes, sequence numbers 7 and 8.
void openTwo(PacketCipher& rx, const std::string& wire, std::size_t step) {
    std::string in, payload;
    std::vector<std::string> got;
    uint32_t seq = 7;
    for (std::size_t at = 0; at < wire.size(); at += step) {
        in += wire.substr(at, step);
        while (true) {
            const auto r = rx.open(in, seq, payload);
            REQUIRE(r != PacketCipher::Open::Bad);
            if (r == PacketCipher::Open::More) break;
            got.push_back(payload);
            ++seq;
        }
    }
    REQUIRE(got.size() == 2);
    CHECK(got[0] == PAYLOAD_1);
    CHECK(got[1] == PAYLOAD_2);
    CHECK(in.empty());
}

}  // namespace

TEST_CASE("SSH: packets of another implementation opened, AES-CTR with HMAC (both orders) and AES-GCM") {
    for (const std::size_t step : {std::size_t(1), std::size_t(5), std::size_t(1000)}) {
        CAPTURE(step);
        PacketCipher ctr, etm, gcm;
        REQUIRE(ctr.start("aes256-ctr", "hmac-sha2-256", PACKET_KEY, PACKET_IV16, PACKET_MAC_KEY).empty());
        openTwo(ctr, KAT_CTR, step);
        REQUIRE(etm.start("aes256-ctr", "hmac-sha2-256-etm@openssh.com", PACKET_KEY, PACKET_IV16, PACKET_MAC_KEY)
                    .empty());
        openTwo(etm, KAT_ETM, step);
        REQUIRE(gcm.start("aes256-gcm@openssh.com", "", PACKET_KEY, PACKET_IV12, "").empty());
        openTwo(gcm, KAT_GCM, step);
    }
}

TEST_CASE("SSH: packets sealed and opened again, a changed byte or sequence number refused, lengths bounded") {
    struct Algorithms {
        const char* cipher;
        const char* mac;
    };
    for (const auto& a : {Algorithms{"aes256-gcm@openssh.com", ""}, Algorithms{"aes128-gcm@openssh.com", ""},
                          Algorithms{"aes256-ctr", "hmac-sha2-256"},
                          Algorithms{"aes128-ctr", "hmac-sha2-256-etm@openssh.com"}}) {
        CAPTURE(a.cipher);
        CAPTURE(a.mac);
        const auto key = randomBytes(PacketCipher::keySize(a.cipher));
        const auto iv  = randomBytes(PacketCipher::ivSize(a.cipher));
        const auto mk  = randomBytes(PacketCipher::macKeySize(a.mac));
        PacketCipher tx, rx;
        REQUIRE(tx.start(a.cipher, a.mac, key, iv, mk).empty());
        REQUIRE(rx.start(a.cipher, a.mac, key, iv, mk).empty());
        std::string wire;
        for (uint32_t seq = 0; seq < 20; ++seq) wire += tx.seal(std::string(seq * 997 % 3000 + 1, char('a' + seq)), seq);
        std::string payload;
        for (uint32_t seq = 0; seq < 20; ++seq) {
            REQUIRE(rx.open(wire, seq, payload) == PacketCipher::Open::Packet);
            CHECK(payload == std::string(seq * 997 % 3000 + 1, char('a' + seq)));
        }
        CHECK(wire.empty());

        // Tampered with: one bit of the last byte (the tag's), or of the ciphertext.
        for (const int where : {-1, 6}) {
            PacketCipher t2, r2;
            REQUIRE(t2.start(a.cipher, a.mac, key, iv, mk).empty());
            REQUIRE(r2.start(a.cipher, a.mac, key, iv, mk).empty());
            auto bad = t2.seal("\x5esome data", 0);
            bad[where < 0 ? bad.size() - 1 : static_cast<std::size_t>(where)] ^= 1;
            CHECK(r2.open(bad, 0, payload) == PacketCipher::Open::Bad);
        }
        // Replayed as another sequence number: the MAC covers it (AES-GCM: the nonce counts the packets instead).
        if (!PacketCipher::aead(a.cipher)) {
            PacketCipher t3, r3;
            REQUIRE(t3.start(a.cipher, a.mac, key, iv, mk).empty());
            REQUIRE(r3.start(a.cipher, a.mac, key, iv, mk).empty());
            auto p = t3.seal("\x5e" "data", 4);
            CHECK(r3.open(p, 5, payload) == PacketCipher::Open::Bad);
        }
    }
    // In the clear (before the first keys): a length beyond 35000 bytes is refused before anything waits for it.
    PacketCipher clear;
    std::string huge = Writer().u32(40000).data() + std::string(16, '\0');
    std::string payload;
    CHECK(clear.open(huge, 0, payload) == PacketCipher::Open::Bad);
    std::string tiny = Writer().u32(2).data() + std::string(16, '\0');
    CHECK(clear.open(tiny, 0, payload) == PacketCipher::Open::Bad);
    auto sealed = clear.seal("\x14kexinit", 0);
    CHECK(sealed.size() % 8 == 0);
    CHECK(sealed.size() >= 16);
    CHECK(clear.open(sealed, 0, payload) == PacketCipher::Open::Packet);
    CHECK(payload == "\x14kexinit");
}

TEST_CASE("Tunnel: the public address in a service's session text") {
    // localhost.run, its QR code in escape sequences and an advertisement after.
    CHECK(findPublicUrl("\x1b[7m  \x1b[0m\r\nabc123def.app.lhr.life tunneled with tls termination, "
                        "https://abc123def.app.lhr.life\r\ncreate an account... see "
                        "https://localhost.run/docs/forever-free/ for more information.\r\n") ==
          "https://abc123def.app.lhr.life");
    // A line that tells of the tunnel wins over an address before it; a newer one replaces it.
    CHECK(findPublicUrl("see https://example.com/docs\nForwarding HTTP traffic from https://Sub.Serveo.net\n") ==
          "https://sub.serveo.net");
    CHECK(findPublicUrl("a.lhr.life tunneled, https://a.lhr.life\nb.lhr.life tunneled, https://b.lhr.life\n") ==
          "https://b.lhr.life");
    // Otherwise the first address; nothing else than a host is taken.
    CHECK(findPublicUrl("Hello\nhttps://x.example.org/<script>\n") == "https://x.example.org");
    CHECK_FALSE(findPublicUrl("no address here, https://localhost only").has_value());
    CHECK_FALSE(findPublicUrl("").has_value());
}

TEST_CASE("Tunnel: --tunnel's value") {
    std::string error;
    const auto lr = parseTunnel("localhost.run", error);
    REQUIRE(lr);
    CHECK(lr->host == "localhost.run");
    CHECK(lr->user == "nokey");
    CHECK(lr->remote_port == 80);
    CHECK_FALSE(lr->pinned.empty());
    CHECK(parseTunnel("", error).has_value());

    const auto own = parseTunnel("ssh://me@example.org:2222/8081", error);
    REQUIRE(own);
    CHECK(own->host == "example.org");
    CHECK(own->port == 2222);
    CHECK(own->user == "me");
    CHECK(own->remote_port == 8081);
    CHECK(own->pinned.empty());
    const auto plain = parseTunnel("ssh://a@serveo.net", error);
    REQUIRE(plain);
    CHECK(plain->port == 22);
    CHECK(plain->remote_port == 80);

    CHECK_FALSE(parseTunnel("serveo.net", error).has_value());
    CHECK_FALSE(parseTunnel("ssh://serveo.net", error).has_value());
    CHECK_FALSE(parseTunnel("ssh://a@host:99999", error).has_value());
    CHECK_FALSE(parseTunnel("ssh://a@host/x", error).has_value());
    CHECK_FALSE(parseTunnel("ssh://a@", error).has_value());
}

TEST_CASE("Tunnel: host keys trusted on first use, kept by host and port, forgotten on demand") {
    const auto dir = std::filesystem::temp_directory_path() / "fcg-known-hosts-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto file = dir / "known_hosts.json";
    CHECK(knownHostKey(file, "example.org", 22).empty());
    rememberHostKey(file, "example.org", 22, "SHA256:aaa");
    rememberHostKey(file, "example.org", 2222, "SHA256:bbb");
    CHECK(knownHostKey(file, "example.org", 22) == "SHA256:aaa");
    CHECK(knownHostKey(file, "example.org", 2222) == "SHA256:bbb");
    rememberHostKey(file, "example.org", 22, "");
    CHECK(knownHostKey(file, "example.org", 22).empty());
    CHECK(knownHostKey(file, "example.org", 2222) == "SHA256:bbb");
    // A file that is not ours is read as empty.
    std::ofstream(file) << "not json";
    CHECK(knownHostKey(file, "example.org", 2222).empty());
    std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------------------------------------------
// A scripted SSH server on this machine: the server's side of everything the tunnel does, checked step by step.

namespace {

// An RSA key for the fake server, made once (PSA), as an SSH blob and a signer.
struct ServerKey {
    mbedtls_svc_key_id_t id = 0;
    std::string blob;

    ServerKey() {
        REQUIRE(startCrypto());
        psa_key_attributes_t a = PSA_KEY_ATTRIBUTES_INIT;
        psa_set_key_type(&a, PSA_KEY_TYPE_RSA_KEY_PAIR);
        psa_set_key_bits(&a, 2048);
        psa_set_key_algorithm(&a, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_ANY_HASH));
        psa_set_key_usage_flags(&a, PSA_KEY_USAGE_SIGN_MESSAGE);
        REQUIRE(psa_generate_key(&a, &id) == PSA_SUCCESS);
        // RSAPublicKey ::= SEQUENCE { modulus INTEGER, publicExponent INTEGER }, short DER read.
        std::string der(600, '\0');
        std::size_t n = 0;
        REQUIRE(psa_export_public_key(id, reinterpret_cast<uint8_t*>(der.data()), der.size(), &n) == PSA_SUCCESS);
        der.resize(n);
        std::size_t at = 0;
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
            REQUIRE(der[at++] == 0x02);
            const auto len = length();
            const auto v   = der.substr(at, len);
            at += len;
            return v;
        };
        REQUIRE(der[at++] == 0x30);
        length();
        const auto modulus  = integer();
        const auto exponent = integer();
        blob = Writer().string("ssh-rsa").mpint(exponent).mpint(modulus).take();
    }
    std::string sign(const std::string& data) const {
        std::string sig(256, '\0');
        std::size_t n = 0;
        REQUIRE(psa_sign_message(id, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_512),
                                 reinterpret_cast<const uint8_t*>(data.data()), data.size(),
                                 reinterpret_cast<uint8_t*>(sig.data()), sig.size(), &n) == PSA_SUCCESS);
        sig.resize(n);
        return Writer().string("rsa-sha2-512").string(sig).take();
    }
};

const ServerKey& serverKey() {
    static const ServerKey key;
    return key;
}

class FakeServer {
   public:
    struct Script {
        std::string ciphers = "aes256-gcm@openssh.com";
        std::string macs    = "hmac-sha2-256";
        bool strict         = true;
        bool rekey          = false;  // a new key exchange, asked by the server, before the forwarded connection
        bool refuse_auth    = false;
        std::string request = "GET /state.json HTTP/1.0\r\n\r\n";  // sent on the forwarded connection
        int viewers         = 1;                                     // connections forwarded at once
        bool abandon        = false;  // first one the viewer leaves at once, before the answer
    };

    FakeServer(uint16_t port, Script script) : script_(std::move(script)) {
        REQUIRE(listener_.listen(port).empty());
        thread_ = std::thread([this] {
            try {
                serve();
            } catch (const std::exception& e) {
                if (error.empty()) error = e.what();
            }
            done = true;
        });
    }
    ~FakeServer() {
        quit_ = true;
        if (thread_.joinable()) thread_.join();
    }

    // What the scenario saw, read once `done`.
    std::atomic_bool done{false};
    std::string error;
    std::string answer;  // the web server's, through the tunnel, to the first viewer
    int answered = 0;    // viewers who had an answer
    int key_exchanges = 0;
    bool disconnected = false;  // the client said goodbye

   private:
    struct Failure : std::runtime_error {
        using std::runtime_error::runtime_error;
    };
    void expect(bool ok, const std::string& what) {
        if (!ok) throw Failure(what);
    }

    void flush() {
        const auto until = std::chrono::steady_clock::now() + 5s;
        while (!out_.empty()) {
            expect(std::chrono::steady_clock::now() < until, "cannot send");
            std::vector<net::Wait> waits{{socket_->handle(), false, true}};
            net::waitAny(waits, 50);
            const auto n = socket_->send(out_);
            expect(n >= 0, "send failed");
            out_.erase(0, static_cast<std::size_t>(n));
        }
    }
    void send(const std::string& payload) {
        out_ += tx_.seal(payload, seq_tx_++);
        flush();
    }
    // The next message, keepalives answered on the way.
    std::string next() {
        const auto until = std::chrono::steady_clock::now() + 10s;
        while (true) {
            std::string payload;
            const auto r = rx_.open(in_, seq_rx_, payload);
            expect(r != PacketCipher::Open::Bad, "bad packet from the client");
            if (r == PacketCipher::Open::Packet) {
                ++seq_rx_;
                if (payload[0] == GLOBAL_REQUEST) {
                    Reader g(std::string_view(payload).substr(1));
                    if (g.string() == "keepalive@openssh.com") {
                        send(Writer().byte(REQUEST_FAILURE).take());
                        continue;
                    }
                }
                return payload;
            }
            expect(!quit_ && std::chrono::steady_clock::now() < until, "nothing from the client");
            std::vector<net::Wait> waits{{socket_->handle(), true, false}};
            net::waitAny(waits, 50);
            if (waits[0].can_read) expect(socket_->receive(in_, 65536) >= 0, "the client closed the connection");
        }
    }
    std::string nextOf(uint8_t type) {
        auto payload = next();
        expect(static_cast<uint8_t>(payload[0]) == type,
               "expected message " + std::to_string(type) + ", got " + std::to_string(uint8_t(payload[0])));
        return payload;
    }

    void kex(bool first) {
        const auto lists =
            Writer()
                .byte(KEXINIT)
                .raw(randomBytes(16))
                .string(script_.strict && first ? "curve25519-sha256,kex-strict-s-v00@openssh.com"
                                                : "curve25519-sha256")
                .string("rsa-sha2-512")
                .string(script_.ciphers)
                .string(script_.ciphers)
                .string(script_.macs)
                .string(script_.macs)
                .string("none")
                .string("none")
                .string("")
                .string("")
                .boolean(false)
                .u32(0)
                .take();
        send(lists);
        const auto i_c = nextOf(KEXINIT);
        const auto init = nextOf(KEX_ECDH_INIT);
        Reader r(std::string_view(init).substr(1));
        const std::string q_c(r.string());
        X25519 ecdh;
        const auto secret = ecdh.agree(q_c);
        expect(secret.has_value(), "bad client key");
        const auto k = Writer().mpint(*secret).take();
        const auto h = sha256(Writer()
                                  .string(v_c_)
                                  .string(V_S)
                                  .string(i_c)
                                  .string(lists)
                                  .string(serverKey().blob)
                                  .string(q_c)
                                  .string(ecdh.publicKey())
                                  .raw(k)
                                  .data());
        if (session_id_.empty()) session_id_ = h;
        send(Writer()
                 .byte(KEX_ECDH_REPLY)
                 .string(serverKey().blob)
                 .string(ecdh.publicKey())
                 .string(serverKey().sign(h))
                 .take());
        send(Writer().byte(NEWKEYS).take());
        const auto derive = [&](char letter, std::size_t size) { return deriveKey(k, h, letter, session_id_, size); };
        // One cipher offered: the one agreed.
        const auto cipher = script_.ciphers;
        const auto mac    = PacketCipher::aead(cipher) ? std::string() : script_.macs;
        expect(tx_.start(cipher, mac, derive('D', PacketCipher::keySize(cipher)),
                         derive('B', PacketCipher::ivSize(cipher)), derive('F', PacketCipher::macKeySize(mac)))
                   .empty(),
               "keys");
        if (script_.strict) seq_tx_ = 0;
        nextOf(NEWKEYS);
        expect(rx_.start(cipher, mac, derive('C', PacketCipher::keySize(cipher)),
                         derive('A', PacketCipher::ivSize(cipher)), derive('E', PacketCipher::macKeySize(mac)))
                   .empty(),
               "keys");
        if (script_.strict) seq_rx_ = 0;
        ++key_exchanges;
    }

    void serve() {
        std::optional<net::Handle> fd;
        const auto until = std::chrono::steady_clock::now() + 10s;
        while (!fd && !quit_ && std::chrono::steady_clock::now() < until) fd = listener_.accept(50);
        expect(fd.has_value(), "no client");
        socket_ = std::make_unique<net::Socket>(*fd);
        out_    = std::string(V_S) + "\r\n";
        flush();
        // The client's version line.
        while (in_.find("\r\n") == std::string::npos) {
            std::vector<net::Wait> waits{{socket_->handle(), true, false}};
            net::waitAny(waits, 50);
            expect(!quit_ && socket_->receive(in_, 1) >= 0, "no version");
        }
        v_c_ = in_.substr(0, in_.find("\r\n"));
        in_.erase(0, v_c_.size() + 2);
        expect(v_c_.rfind("SSH-2.0-", 0) == 0, "bad version " + v_c_);

        kex(true);
        const auto service_message = nextOf(SERVICE_REQUEST);
        Reader service(std::string_view(service_message).substr(1));
        expect(service.string() == "ssh-userauth", "service");
        send(Writer().byte(SERVICE_ACCEPT).string("ssh-userauth").take());
        const auto auth_message = nextOf(USERAUTH_REQUEST);
        Reader auth(std::string_view(auth_message).substr(1));
        expect(auth.string() == "nokey", "user");
        auth.string();
        expect(auth.string() == "none", "method");
        if (script_.refuse_auth) {
            send(Writer().byte(USERAUTH_FAILURE).string("publickey").boolean(false).take());
            next();  // the client goes
            return;
        }
        send(Writer().byte(USERAUTH_SUCCESS).take());

        const auto forward_message = nextOf(GLOBAL_REQUEST);
        Reader forward(std::string_view(forward_message).substr(1));
        expect(forward.string() == "tcpip-forward", "forward");
        expect(forward.boolean(), "want reply");
        forward.string();
        expect(forward.u32() == 80, "port");
        send(Writer().byte(REQUEST_SUCCESS).take());

        const auto open_message = nextOf(CHANNEL_OPEN);
        Reader open(std::string_view(open_message).substr(1));
        expect(open.string() == "session", "session");
        const auto session = open.u32();
        send(Writer().byte(CHANNEL_OPEN_CONFIRMATION).u32(session).u32(100).u32(1 << 20).u32(32768).take());
        const auto shell_message = nextOf(CHANNEL_REQUEST);
        Reader shell(std::string_view(shell_message).substr(1));
        shell.u32();
        expect(shell.string() == "shell", "shell");
        send(Writer()
                 .byte(CHANNEL_DATA)
                 .u32(session)
                 .string("Welcome\r\nabc123.lhr.life tunneled with tls termination, https://abc123.lhr.life\r\n")
                 .take());

        if (script_.rekey) kex(false);

        // Viewers at once: forwarded connections, their requests, the web server's answers through the tunnel. With
        // `abandon`, one more (300) whose viewer goes at once: closed by the server before the answer is relayed.
        std::vector<uint32_t> senders;
        for (int i = 0; i < script_.viewers; ++i) senders.push_back(200 + static_cast<uint32_t>(i));
        if (script_.abandon) senders.push_back(300);
        for (const auto sender : senders) {
            send(Writer()
                     .byte(CHANNEL_OPEN)
                     .string("forwarded-tcpip")
                     .u32(sender)
                     .u32(1 << 20)
                     .u32(32768)
                     .string("localhost")
                     .u32(80)
                     .string("203.0.113.9")
                     .u32(5555)
                     .take());
        }
        std::map<uint32_t, uint32_t> channels;  // ours, the client's
        std::map<uint32_t, std::string> answers;
        std::size_t closed = 0;
        while (closed < senders.size()) {
            const auto m = next();
            Reader r(std::string_view(m).substr(1));
            const auto recipient = r.u32();
            if (m[0] == CHANNEL_OPEN_CONFIRMATION) {
                const auto channel  = r.u32();
                channels[recipient] = channel;
                send(Writer().byte(CHANNEL_DATA).u32(channel).string(script_.request).take());
                if (recipient == 300) {
                    send(Writer().byte(CHANNEL_EOF).u32(channel).take());
                    send(Writer().byte(CHANNEL_CLOSE).u32(channel).take());
                }
                continue;
            }
            expect(channels.count(recipient) == 1, "unknown recipient");
            if (m[0] == CHANNEL_DATA) {
                answers[recipient] += r.string();
            } else if (m[0] == CHANNEL_CLOSE) {
                if (recipient != 300) send(Writer().byte(CHANNEL_CLOSE).u32(channels[recipient]).take());
                ++closed;
            } else {
                expect(m[0] == CHANNEL_EOF, "unexpected message " + std::to_string(uint8_t(m[0])));
            }
        }
        for (const auto& [recipient, text] : answers) {
            if (recipient != 300) answered += text.rfind("HTTP/1.0 200 OK", 0) == 0 ? 1 : 0;
        }
        answer = answers[200];

        // Until the client goes.
        while (!quit_) {
            std::string m;
            try {
                m = next();
            } catch (const Failure&) {
                if (quit_) return;
                continue;
            }
            if (m[0] == DISCONNECT) {
                disconnected = true;
                return;
            }
        }
    }

    static constexpr const char* V_S = "SSH-2.0-FakeServer_1.0";
    Script script_;
    net::TcpListener listener_;
    std::unique_ptr<net::Socket> socket_;
    std::thread thread_;
    std::atomic_bool quit_{false};
    std::string in_, out_, v_c_, session_id_;
    PacketCipher rx_, tx_;
    uint32_t seq_rx_ = 0, seq_tx_ = 0;
};

constexpr uint16_t SSH_PORT = 26411;
constexpr uint16_t WEB_PORT = 26413;

TunnelSettings loopback(const std::string& pinned) {
    TunnelSettings s;
    s.host        = "127.0.0.1";
    s.port        = SSH_PORT;
    s.user        = "nokey";
    s.local_port  = WEB_PORT;
    s.pinned      = pinned;
    s.first_retry = 30s;  // no second attempt within a test
    return s;
}

template <typename Done>
bool waitFor(Done done, std::chrono::milliseconds limit = 15s) {
    const auto until = std::chrono::steady_clock::now() + limit;
    while (!done()) {
        if (std::chrono::steady_clock::now() > until) return false;
        std::this_thread::sleep_for(20ms);
    }
    return true;
}

}  // namespace

TEST_CASE("Tunnel: a viewer's request relayed to the web server and back, through each cipher, with a rekey") {
    WebServer web;
    REQUIRE(web.start(WEB_PORT).empty());
    struct Case {
        const char* ciphers;
        const char* macs;
        bool strict;
        bool rekey;
        int viewers;
        bool abandon;
    };
    for (const auto& c : {Case{"aes256-gcm@openssh.com", "hmac-sha2-256", true, false, 5, true},
                          Case{"aes256-ctr", "hmac-sha2-256", true, true, 1, false},
                          Case{"aes128-ctr", "hmac-sha2-256-etm@openssh.com", false, true, 3, false},
                          Case{"aes128-gcm@openssh.com", "hmac-sha2-256", false, false, 1, true}}) {
        CAPTURE(c.ciphers);
        CAPTURE(c.macs);
        CAPTURE(c.rekey);
        FakeServer::Script script;
        script.ciphers = c.ciphers;
        script.macs    = c.macs;
        script.strict  = c.strict;
        script.rekey   = c.rekey;
        script.viewers = c.viewers;
        script.abandon = c.abandon;
        FakeServer server(SSH_PORT, script);
        SshTunnel tunnel;
        tunnel.start(loopback(fingerprint(serverKey().blob)));
        CHECK(waitFor([&] { return !server.answer.empty() && tunnel.status().connections == 0; }));
        const auto status = tunnel.status();
        CHECK(status.state == TunnelState::OnAir);
        CHECK(status.url == "https://abc123.lhr.life");
        tunnel.stop();
        CHECK(waitFor([&] { return server.done.load(); }));
        CHECK_MESSAGE(server.error.empty(), server.error);
        CHECK(server.answer.rfind("HTTP/1.0 200 OK", 0) == 0);
        CHECK(server.answer.find("application/json") != std::string::npos);
        CHECK(server.answered == c.viewers);
        CHECK(server.key_exchanges == (c.rekey ? 2 : 1));
        CHECK(server.disconnected);
        CHECK(tunnel.status().state == TunnelState::Off);
    }
}

TEST_CASE("Tunnel: a host key not the one pinned, or not the one trusted before, stops it for good") {
    const auto dir = std::filesystem::temp_directory_path() / "fcg-tunnel-keys-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto known = dir / "known_hosts.json";

    {
        FakeServer server(SSH_PORT, {});
        SshTunnel tunnel;
        tunnel.start(loopback("SHA256:notTheKey"));
        CHECK(waitFor([&] { return tunnel.status().state == TunnelState::Failed; }));
        CHECK(tunnel.status().error.find("not the one expected") != std::string::npos);
        CHECK(tunnel.status().url.empty());
    }
    // First use: trusted and kept.
    {
        WebServer web;
        REQUIRE(web.start(WEB_PORT).empty());
        FakeServer server(SSH_PORT, {});
        auto settings        = loopback("");
        settings.known_hosts = known;
        SshTunnel tunnel;
        tunnel.start(settings);
        CHECK(waitFor([&] { return !server.answer.empty(); }));
        CHECK(tunnel.status().state == TunnelState::OnAir);
        CHECK(knownHostKey(known, "127.0.0.1", SSH_PORT) == fingerprint(serverKey().blob));
    }
    // Then another key: refused, the new one offered to the user.
    {
        rememberHostKey(known, "127.0.0.1", SSH_PORT, "SHA256:anotherKey");
        FakeServer server(SSH_PORT, {});
        auto settings        = loopback("");
        settings.known_hosts = known;
        SshTunnel tunnel;
        tunnel.start(settings);
        CHECK(waitFor([&] { return tunnel.status().state == TunnelState::Failed; }));
        CHECK(tunnel.status().new_key == fingerprint(serverKey().blob));
        CHECK(tunnel.status().error.find("changed") != std::string::npos);
        CHECK(knownHostKey(known, "127.0.0.1", SSH_PORT) == "SHA256:anotherKey");
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("Tunnel: a server that wants a key is an error trying again cannot mend") {
    FakeServer::Script script;
    script.refuse_auth = true;
    FakeServer server(SSH_PORT, script);
    SshTunnel tunnel;
    tunnel.start(loopback(fingerprint(serverKey().blob)));
    CHECK(waitFor([&] { return tunnel.status().state == TunnelState::Failed; }));
    CHECK(tunnel.status().error.find("publickey") != std::string::npos);
}

TEST_CASE("Tunnel: no server: reconnecting, with the reason") {
    // Nothing listens on this port.
    SshTunnel tunnel;
    tunnel.start(loopback(""));
    CHECK(waitFor([&] { return tunnel.status().state == TunnelState::Reconnecting; }));
    CHECK_FALSE(tunnel.status().error.empty());
    tunnel.stop();
    CHECK(tunnel.status().state == TunnelState::Off);
}

TEST_CASE("Tunnel: localhost.run gives a public address (FCG_NET_TESTS=1 only)") {
    if (!netTests()) {
        MESSAGE("needs the internet: FCG_NET_TESTS=1, skipped");
        return;
    }
    // Port 1: nothing listens there, so nothing is served while the tunnel is up, a few seconds at most.
    SshTunnel tunnel;
    tunnel.start(localhostRun(1));
    TunnelStatus status;
    waitFor(
        [&] {
            status = tunnel.status();
            return !status.url.empty() || status.state == TunnelState::Failed ||
                   status.state == TunnelState::Reconnecting;
        },
        30s);
    tunnel.stop();
    MESSAGE("state ", static_cast<int>(status.state), ", address ", status.url, ", error ", status.error);
    CHECK(status.state == TunnelState::OnAir);
    CHECK(status.url.rfind("https://", 0) == 0);
}

TEST_CASE("Tunnel: the broadcaster runs it with the web page only, and stops it with the broadcast") {
    Broadcaster broadcaster;
    BroadcastSettings settings;
    settings.tunnel    = true;
    settings.tunnel_to = loopback("");  // nothing listens there: it keeps trying
    // Without the web page: no tunnel.
    broadcaster.configure(settings);
    CHECK(broadcaster.tunnel().state == TunnelState::Off);

    settings.web      = true;
    settings.web_port = WEB_PORT;
    broadcaster.configure(settings);
    CHECK(broadcaster.tunnel().state != TunnelState::Off);
    CHECK(waitFor([&] { return broadcaster.tunnel().state == TunnelState::Reconnecting; }));

    settings.tunnel = false;
    broadcaster.configure(settings);
    CHECK(broadcaster.tunnel().state == TunnelState::Off);
    CHECK(broadcaster.web().running());

    settings.tunnel = true;
    broadcaster.configure(settings);
    CHECK(broadcaster.tunnel().state != TunnelState::Off);
    broadcaster.stop();
    CHECK(broadcaster.tunnel().state == TunnelState::Off);
}

TEST_CASE("Tunnel: the setup's tunnel settings") {
    MatchSetup setup;
    setup.web_port   = 8123;
    const auto known = std::filesystem::path("known_hosts.json");
    const auto lr    = tunnelSettings(setup, known);
    CHECK(lr.host == "localhost.run");
    CHECK(lr.local_port == 8123);
    CHECK_FALSE(lr.pinned.empty());
    CHECK(lr.known_hosts.empty());

    setup.tunnel_service     = 1;
    setup.tunnel_host        = "example.org";
    setup.tunnel_port        = 2222;
    setup.tunnel_user        = "me";
    setup.tunnel_remote_port = 0;
    const auto own           = tunnelSettings(setup, known);
    CHECK(own.host == "example.org");
    CHECK(own.port == 2222);
    CHECK(own.user == "me");
    CHECK(own.remote_port == 0);
    CHECK(own.pinned.empty());
    CHECK(own.known_hosts == known);

    // Saved and read back; a setup of an earlier version, without them, has the defaults.
    const nlohmann::json json = setup;
    const auto back           = json.get<MatchSetup>();
    CHECK(back.tunnel_host == "example.org");
    CHECK(back.tunnel_service == 1);
    auto old = json;
    for (const char* key : {"broadcast_tunnel", "tunnel_service", "tunnel_host", "tunnel_port", "tunnel_user",
                            "tunnel_remote_port"}) {
        old.erase(key);
    }
    const auto earlier = old.get<MatchSetup>();
    CHECK_FALSE(earlier.broadcast_tunnel);
    CHECK(earlier.tunnel_service == 0);
    CHECK(earlier.tunnel_port == 22);
    CHECK(earlier.tunnel_remote_port == 80);
}

TEST_CASE("QR code: a localhost.run address fits in version 3, its finder patterns in the corners") {
    const auto qr = makeQr("https://20e15a40a90d7a.app.lhr.life");
    REQUIRE(qr.size == 29);  // version 3: 17 + 4 * 3 modules
    REQUIRE(qr.dark.size() == 29u * 29u);
    // The three finder patterns: a dark ring, a light ring, a dark centre; the fourth corner has none.
    for (const auto& [x0, y0] : {std::pair{0, 0}, std::pair{22, 0}, std::pair{0, 22}}) {
        CHECK(qr.at(x0, y0));
        CHECK(qr.at(x0 + 6, y0 + 6));
        CHECK_FALSE(qr.at(x0 + 1, y0 + 1));
        CHECK(qr.at(x0 + 3, y0 + 3));
    }
    // Another address, another code; a longer one, a larger code.
    CHECK(makeQr("https://ffffffffffffff.app.lhr.life").dark != qr.dark);
    CHECK(makeQr("https://" + std::string(100, 'a') + ".example.org").size > 29);
    // Beyond what a QR code holds: none.
    CHECK(makeQr(std::string(5000, 'x')).size == 0);
}
