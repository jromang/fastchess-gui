#include <doctest/doctest.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <libssh2.h>  // before windows.h: it includes winsock2.h
#include <psa/crypto.h>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <unistd.h>
#endif

#include "broadcast.hpp"
#include "cpu_time.hpp"
#include "broadcaster.hpp"
#include "libssh2_backend.hpp"
#include "qr.hpp"
#include "net.hpp"
#include "ssh_tunnel.hpp"
#include "ssh_wire.hpp"

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

std::string readText(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

bool netTests() {
    const char* wanted = std::getenv("FCG_NET_TESTS");
    return wanted && std::string(wanted) == "1";
}

unsigned char* bytes(std::string& s) { return reinterpret_cast<unsigned char*>(s.data()); }
const unsigned char* bytes(const std::string& s) { return reinterpret_cast<const unsigned char*>(s.data()); }

// Known answers: an RSA key made for the tests only (Python's cryptography), and localhost.run's host key as
// ssh-keyscan fetched it, with two of its signatures of exchange hashes (one key exchange each, rsa-sha2-512 and
// rsa-sha2-256, recorded in November 2026).
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

const std::string LOCALHOST_RUN_H512 = unhex(
    "5f31cd8cf2ac91f43caf683c22251c77e2b0334ab8fb7a57e98325e4f1c68b32");

const std::string LOCALHOST_RUN_SIG512 = unhex(
    "0000000c7273612d736861322d353132000001002a68b082e43e062a5fc7fb57fc127b694c760363110c52061a7e47bc"
    "c5ed2ef23b2c61d9d70d509291eaa90ef23c158a03bc78ab883b96a6ef8ba734492837a2586cf90915264ff1b0f43fff"
    "914e64275b700e944497630ccdb38e5c8615d885c5397e8fe21949daeeec47b035771f635c3958c9da6b12da07ad091f"
    "295420a6f93ae973e75d5ef81a3f54e1563598a418b41f44f8380c2a2d62eb31fd4698db820a341d8c8868824aaa6848"
    "b2fac5ee957bb13ec516b39fd4199a87819f6a0a882b4f4cd26232d4c582185430d8651b6a665372d0b533626bdb1126"
    "36ba609adb7b74b730d8fd2810836e0519c779627be7bdb2fd5eea273024450f45282942");

const std::string LOCALHOST_RUN_H256 = unhex(
    "30ce9432855dc291273849c29806fea275be7518223cab488da6b50dddbbf414");

const std::string LOCALHOST_RUN_SIG256 = unhex(
    "0000000c7273612d736861322d32353600000100914b2830bd6880a86c8e7e2ba1123be0d9d347f9f79602dae970476f"
    "b586c5519a9d21511faca2e0d677595a1a5b931c6e820ac7e7c6d62cdfc8c7a311c75f85e55fb6a9e22c221a5be42916"
    "6bd8418221c1728c42881946b9af12553d442286bcd87dffffe3c121855cd1216fa5d20bad605a0592f3f033299d12cc"
    "319b903cb14ae82e6758d1caf1b502efd696c6d3a416774056fbc1a4f45b38853d278f2ff8e92f38a2b5208dfd858ffc"
    "3ea624b31d1ad0bb22d0b155634c2021fbde5df329dc25ed64dfcfc4c76a427e83e4088ef175deb661fdd1fc01cb4634"
    "6618f534b0432ba2da84a67b4c2714385965417bead4221b901cd3241fcd4293d8a26d98");

// A libssh2 session for the backend's allocations, not connected.
struct TestSession {
    LIBSSH2_SESSION* s = nullptr;
    TestSession() {
        REQUIRE(libssh2_init(0) == 0);
        s = libssh2_session_init();
        REQUIRE(s != nullptr);
    }
    ~TestSession() { libssh2_session_free(s); }
};

// An RSA public key of the backend from an "ssh-rsa" blob: nullptr when it refuses it.
ssh2_rsa_ctx* rsaKey(LIBSSH2_SESSION* session, const std::string& blob) {
    Reader r(blob);
    r.string();
    const std::string e(r.string()), n(r.string());
    ssh2_rsa_ctx* rsa = nullptr;
    if (!r.ok() || ssh2_rsa_new(&rsa, session, bytes(e), e.size(), bytes(n), n.size(), nullptr, 0, nullptr, 0,
                                nullptr, 0, nullptr, 0, nullptr, 0, nullptr, 0) != 0) {
        return nullptr;
    }
    return rsa;
}

// Whether `signature` (the algorithm's name, then the signature) of `data` verifies with `hash_len`.
bool rsaVerifies(LIBSSH2_SESSION* session, ssh2_rsa_ctx* rsa, std::size_t hash_len, const std::string& signature,
                 const std::string& data) {
    Reader r(signature);
    r.string();
    const std::string value(r.string());
    return r.ok() && ssh2_rsa_sha2_verify(rsa, session, hash_len, bytes(value), value.size(), bytes(data),
                                          data.size()) == 0;
}

// X25519 through the backend: the shared secret of `private_key` and `peer`, as the bytes of the number libssh2
// carries; nullopt when it is refused.
std::optional<std::string> x25519(std::string private_key, std::string peer) {
    ssh2_bn* k = ssh2_bn_init();
    std::optional<std::string> out;
    if (ssh2_curve25519_gen_k(&k, bytes(private_key), bytes(peer)) == 0) {
        std::string value((ssh2_bn_bits(k) + 7) / 8, '\0');
        ssh2_bn_to_bin(k, bytes(value));
        out = value;
    }
    ssh2_bn_free(k);
    return out;
}

// The backend's cipher `algo` over `data`, given in calls of `step` bytes as libssh2's packet layer does.
std::string cipher(int algo, std::string key, std::string iv, std::string data, std::size_t step, bool encrypt) {
    ssh2_cipher_ctx ctx;
    REQUIRE(ssh2_cipher_init(&ctx, algo, bytes(iv), bytes(key), encrypt ? 1 : 0) == 0);
    for (std::size_t at = 0; at < data.size(); at += step) {
        REQUIRE(ssh2_cipher_crypt(&ctx, algo, encrypt ? 1 : 0, bytes(data) + at, std::min(step, data.size() - at),
                                  SSH2_MIDDLE_BLOCK) == 0);
    }
    ssh2_cipher_dtor(&ctx);
    return data;
}

// One SSH packet through the backend's AES-GCM as libssh2's transport.c gives it: the length and 12 bytes first,
// 16 at a time, and the last part with the tag (a block and less, at most). False when it is refused.
bool gcmPacket(ssh2_cipher_ctx& ctx, int algo, bool encrypt, std::string& packet) {
    const std::size_t end = packet.size();
    std::size_t at        = 0;
    while (at < end) {
        std::size_t size = std::min<std::size_t>(16, end - at);
        int firstlast    = at == 0 ? SSH2_FIRST_BLOCK : SSH2_MIDDLE_BLOCK;
        if (at > 0 && end - at < 32) {
            size      = end - at;
            firstlast = SSH2_LAST_BLOCK;
        }
        if (ssh2_cipher_crypt(&ctx, algo, encrypt ? 1 : 0, bytes(packet) + at, size, firstlast) != 0) return false;
        at += size;
    }
    return true;
}

const std::string PACKET_KEY = unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
// The GCM invocation counter about to carry.
const std::string PACKET_IV12 = unhex("0a0b0c0d00000000ffffffff");
const std::string PAYLOAD_1   = std::string("\x5ehello, tunnel") + std::string(40, '\0');
const std::string PAYLOAD_2   = "\x5esecond packet";

// Two packets of AES256-GCM made by another implementation (Python's cryptography), PACKET_KEY and PACKET_IV12.
const std::string KAT_GCM = unhex(
    "000000403ddd7b5ee77e46cf712301aea3b525dd54b5b596045f2f2bcc1e3d16c55da015d6cf0de43b65cebfdb0bb981"
    "ef0eeb9b46a96a35fcf0e1138fa6021f3858358aac38cb6f7e68af309b0630a24c46c8ca000000203f23bb6fab8608e1"
    "14cbea18289afe0c3dc55955bf0e4dcc27b21f6843f065a19cdc41c263419fe461ae8791325eb90e");

}  // namespace

TEST_CASE("libssh2 backend: X25519 gives RFC 7748's public keys and shared secret, and refuses a zero secret") {
    TestSession session;
    // RFC 7748, 6.1. The public key is X25519 with the base point, 9.
    const auto alice_private = unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    const auto bob_private   = unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    const auto alice_public  = unhex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    const auto bob_public    = unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    const auto base          = std::string("\x09", 1) + std::string(31, '\0');
    CHECK(x25519(alice_private, base) == alice_public);
    CHECK(x25519(bob_private, base) == bob_public);
    const auto shared = unhex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    CHECK(x25519(alice_private, bob_public) == shared);
    CHECK(x25519(bob_private, alice_public) == shared);
    // Points of small order give zero: refused (RFC 7748 6.1, RFC 8731 3). Zero, one, and a point of order 8.
    CHECK_FALSE(x25519(alice_private, std::string(32, '\0')).has_value());
    CHECK_FALSE(x25519(alice_private, std::string("\x01", 1) + std::string(31, '\0')).has_value());
    CHECK_FALSE(
        x25519(alice_private, unhex("e0eb7a7c3b41b8ae1656e3faf19fc46ada098deb9c32b1fd866205165f49b800")).has_value());
    // Keys made by the backend: each pair agrees.
    for (int i = 0; i < 8; ++i) {
        uint8_t *a_pub = nullptr, *a_priv = nullptr, *b_pub = nullptr, *b_priv = nullptr;
        REQUIRE(ssh2_curve25519_new(session.s, &a_pub, &a_priv) == 0);
        REQUIRE(ssh2_curve25519_new(session.s, &b_pub, &b_priv) == 0);
        const auto as = [](uint8_t* p) { return std::string(reinterpret_cast<char*>(p), 32); };
        const auto ab = x25519(as(a_priv), as(b_pub));
        REQUIRE(ab.has_value());
        CHECK(ab == x25519(as(b_priv), as(a_pub)));
        CHECK(as(a_pub) != as(b_pub));
        for (auto* p : {a_pub, a_priv, b_pub, b_priv}) std::free(p);
    }
}

TEST_CASE("libssh2 backend: RSA host keys, rsa-sha2-512 and rsa-sha2-256 signatures, sizes in bits") {
    TestSession session;
    auto* rsa = rsaKey(session.s, KAT_RSA_BLOB);
    REQUIRE(rsa != nullptr);
    CHECK(rsaVerifies(session.s, rsa, 64, KAT_RSA_SIG512, KAT_RSA_DATA));
    CHECK(rsaVerifies(session.s, rsa, 32, KAT_RSA_SIG256, KAT_RSA_DATA));
    // The hash agreed is the one checked.
    CHECK_FALSE(rsaVerifies(session.s, rsa, 64, KAT_RSA_SIG256, KAT_RSA_DATA));
    CHECK_FALSE(rsaVerifies(session.s, rsa, 20, KAT_RSA_SIG512, KAT_RSA_DATA));
    // Other data, or a signature changed by a bit, or longer than the modulus.
    auto data = KAT_RSA_DATA;
    data[0] ^= 1;
    CHECK_FALSE(rsaVerifies(session.s, rsa, 64, KAT_RSA_SIG512, data));
    auto sig = KAT_RSA_SIG512;
    sig[sig.size() - 1] ^= 1;
    CHECK_FALSE(rsaVerifies(session.s, rsa, 64, sig, KAT_RSA_DATA));
    // Its leading zero left out, as some servers do: padded again. One more byte than the modulus: refused.
    Reader parts(KAT_RSA_SIG512);
    parts.string();
    const std::string value(parts.string());
    REQUIRE(value.size() == 256);
    REQUIRE(value[0] == '\0');
    CHECK(rsaVerifies(session.s, rsa, 64, Writer().string("rsa-sha2-512").string(value.substr(1)).take(),
                      KAT_RSA_DATA));
    CHECK_FALSE(rsaVerifies(session.s, rsa, 64, Writer().string("rsa-sha2-512").string('\0' + value).take(),
                            KAT_RSA_DATA));
    ssh2_rsa_free(rsa, session.s);

    // localhost.run's key, and two of its own signatures.
    rsa = rsaKey(session.s, LOCALHOST_RUN_BLOB);
    REQUIRE(rsa != nullptr);
    CHECK(rsaVerifies(session.s, rsa, 64, LOCALHOST_RUN_SIG512, LOCALHOST_RUN_H512));
    CHECK(rsaVerifies(session.s, rsa, 32, LOCALHOST_RUN_SIG256, LOCALHOST_RUN_H256));
    CHECK_FALSE(rsaVerifies(session.s, rsa, 64, LOCALHOST_RUN_SIG512, LOCALHOST_RUN_H256));
    ssh2_rsa_free(rsa, session.s);

    // The size in bits: 2041 bits take 256 bytes, as 2048 do, and are refused; beyond PSA's 4096 too. A negative
    // number (its high bit set) is no modulus.
    const auto blob = [](const std::string& modulus) {
        return Writer().string("ssh-rsa").mpint(std::string("\x01\x00\x01", 3)).string(modulus).take();
    };
    CHECK(rsaKey(session.s, blob("\x01" + std::string(255, '\xff'))) == nullptr);
    CHECK(rsaKey(session.s, blob(std::string("\x00\x01", 2) + std::string(512, '\xff'))) == nullptr);
    CHECK(rsaKey(session.s, blob(std::string(256, '\xff'))) == nullptr);
    auto* big = rsaKey(session.s, blob(std::string("\x00", 1) + std::string(256, '\xff')));
    CHECK(big != nullptr);
    ssh2_rsa_free(big, session.s);
    // Private keys are not taken.
    const std::string one("\x01", 1);
    ssh2_rsa_ctx* priv = nullptr;
    CHECK(ssh2_rsa_new(&priv, session.s, bytes(one), 1, bytes(one), 1, bytes(one), 1, nullptr, 0, nullptr, 0,
                       nullptr, 0, nullptr, 0, nullptr, 0) != 0);

    CHECK(fingerprint(KAT_RSA_BLOB) == KAT_RSA_FP);
    // As ssh-keygen -l shows localhost.run's key, and as pinned.
    CHECK(fingerprint(LOCALHOST_RUN_BLOB) == "SHA256:FV8IMJ4IYjYUTnd6on7PqbRjaZf4c1EhhEBgeUdE94I");
    CHECK(localhostRun(8080).pinned == fingerprint(LOCALHOST_RUN_BLOB));
}

TEST_CASE("libssh2 backend: AES-CTR as NIST SP 800-38A gives it, its counter carried from call to call") {
    // F.5.1 and F.5.5: CTR-AES128 and CTR-AES256, encrypting; CTR deciphers the same way.
    const auto counter = unhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    const auto plain   = unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"
                               "30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710");
    const auto key128  = unhex("2b7e151628aed2a6abf7158809cf4f3c");
    const auto out128  = unhex("874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff"
                               "5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f3009cee");
    const auto key256  = unhex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
    const auto out256  = unhex("601ec313775789a5b7a7f504bbf3d228f443e3ca4d62b59aca84e990cacaf5c5"
                               "2b0930daa23de94ce87017ba2d84988ddfc9c58db67aada613c2dd08457941a6");
    for (const std::size_t step : {std::size_t(16), std::size_t(32), std::size_t(64)}) {
        CAPTURE(step);
        CHECK(cipher(ssh2_cipher_aes128ctr, key128, counter, plain, step, true) == out128);
        CHECK(cipher(ssh2_cipher_aes256ctr, key256, counter, plain, step, true) == out256);
        CHECK(cipher(ssh2_cipher_aes256ctr, key256, counter, out256, step, false) == plain);
    }
    // The whole 128-bit counter counts: its low half carries into the high one, as SSH wants (RFC 4344, 4).
    const auto carry  = unhex("0001020304050607ffffffffffffffff");
    const auto stream = cipher(ssh2_cipher_aes256ctr, key256, carry, std::string(32, '\0'), 16, true);
    const auto next   = cipher(ssh2_cipher_aes256ctr, key256, unhex("00010203040506080000000000000000"),
                               std::string(16, '\0'), 16, true);
    CHECK(stream.substr(16) == next);
    // A key of the wrong kind is refused.
    ssh2_cipher_ctx ctx;
    auto iv = counter;
    CHECK(ssh2_cipher_init(&ctx, 99, bytes(iv), bytes(iv), 1) != 0);
}

TEST_CASE("libssh2 backend: AES-GCM packets of another implementation opened, sealed again, tampering refused") {
    // Each packet: the length (authenticated, not enciphered), then the padding length, the payload, the padding;
    // the nonce's invocation counter goes from 00000000ffffffff to 0000000100000000 between them.
    ssh2_cipher_ctx rx;
    auto key = PACKET_KEY, iv = PACKET_IV12;
    REQUIRE(ssh2_cipher_init(&rx, ssh2_cipher_aes256gcm, bytes(iv), bytes(key), 0) == 0);
    std::string first = KAT_GCM.substr(0, 4 + 64 + 16), second = KAT_GCM.substr(4 + 64 + 16);
    REQUIRE(second.size() == 4 + 32 + 16);
    const auto sealed_first = first, sealed_second = second;
    REQUIRE(gcmPacket(rx, ssh2_cipher_aes256gcm, false, first));
    REQUIRE(gcmPacket(rx, ssh2_cipher_aes256gcm, false, second));
    ssh2_cipher_dtor(&rx);
    const auto payload = [](const std::string& packet) {
        const auto length  = static_cast<std::size_t>(Reader(packet).u32());
        const auto padding = static_cast<unsigned char>(packet[4]);
        return packet.substr(5, length - 1 - padding);
    };
    CHECK(payload(first) == PAYLOAD_1);
    CHECK(payload(second) == PAYLOAD_2);

    // Sealed again with the same nonces: the same bytes, tags included.
    ssh2_cipher_ctx tx;
    REQUIRE(ssh2_cipher_init(&tx, ssh2_cipher_aes256gcm, bytes(iv), bytes(key), 1) == 0);
    auto again_first = first.substr(0, first.size() - 16) + std::string(16, '\0');
    auto again_second = second.substr(0, second.size() - 16) + std::string(16, '\0');
    REQUIRE(gcmPacket(tx, ssh2_cipher_aes256gcm, true, again_first));
    REQUIRE(gcmPacket(tx, ssh2_cipher_aes256gcm, true, again_second));
    ssh2_cipher_dtor(&tx);
    CHECK(again_first == sealed_first);
    CHECK(again_second == sealed_second);

    // A bit changed in the length, the ciphertext or the tag; or the packets in the other order.
    for (const std::size_t where : {std::size_t(2), std::size_t(20), sealed_first.size() - 1}) {
        CAPTURE(where);
        ssh2_cipher_ctx bad;
        REQUIRE(ssh2_cipher_init(&bad, ssh2_cipher_aes256gcm, bytes(iv), bytes(key), 0) == 0);
        auto tampered = sealed_first;
        tampered[where] ^= 1;
        CHECK_FALSE(gcmPacket(bad, ssh2_cipher_aes256gcm, false, tampered));
        ssh2_cipher_dtor(&bad);
    }
    ssh2_cipher_ctx swapped;
    REQUIRE(ssh2_cipher_init(&swapped, ssh2_cipher_aes256gcm, bytes(iv), bytes(key), 0) == 0);
    auto out_of_order = sealed_second;
    CHECK_FALSE(gcmPacket(swapped, ssh2_cipher_aes256gcm, false, out_of_order));
    ssh2_cipher_dtor(&swapped);

    // AES128-GCM, sealed and opened, longer packets.
    const auto key128 = PACKET_KEY.substr(0, 16);
    for (const std::size_t length : {std::size_t(16), std::size_t(48), std::size_t(4096)}) {
        CAPTURE(length);
        ssh2_cipher_ctx a, b;
        auto k = key128, n = PACKET_IV12;
        REQUIRE(ssh2_cipher_init(&a, ssh2_cipher_aes128gcm, bytes(n), bytes(k), 1) == 0);
        REQUIRE(ssh2_cipher_init(&b, ssh2_cipher_aes128gcm, bytes(n), bytes(k), 0) == 0);
        std::string plain = Writer().u32(static_cast<uint32_t>(length)).take() + std::string(length, 'x');
        auto packet       = plain + std::string(16, '\0');
        REQUIRE(gcmPacket(a, ssh2_cipher_aes128gcm, true, packet));
        CHECK(packet.substr(4, length) != plain.substr(4));
        REQUIRE(gcmPacket(b, ssh2_cipher_aes128gcm, false, packet));
        CHECK(packet.substr(0, 4 + length) == plain);
        ssh2_cipher_dtor(&a);
        ssh2_cipher_dtor(&b);
    }
}

TEST_CASE("libssh2 backend: HMAC as RFC 2202 and RFC 4231 give it; what is not supported fails") {
    const auto hmac = [](psa_algorithm_t alg, std::string key, const std::string& data, std::size_t size) {
        ssh2_hmac_ctx ctx;
        std::string out(size, '\0');
        REQUIRE(ssh2_hmac_ctx_init(&ctx) == 1);
        REQUIRE(ssh2_hmac_init(&ctx, alg, key.data(), key.size()) == 1);
        REQUIRE(ssh2_hmac_update(&ctx, data.data(), data.size()));
        REQUIRE(ssh2_hmac_final(&ctx, out.data(), out.size()) == 1);
        ssh2_hmac_cleanup(&ctx);
        return out;
    };
    const std::string data = "what do ya want for nothing?";
    CHECK(hmac(PSA_ALG_SHA_1, "Jefe", data, 20) == unhex("effcdf6ae5eb2fa2d27416d5f184df9c259a7c79"));
    CHECK(hmac(PSA_ALG_SHA_256, "Jefe", data, 32) ==
          unhex("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
    CHECK(hmac(PSA_ALG_SHA_512, "Jefe", data, 64) ==
          unhex("164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea250554"
                "9758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737"));
    ssh2_hash_ctx hash;
    std::string digest(32, '\0');
    REQUIRE(ssh2_hash_init(&hash, PSA_ALG_SHA_256) == 1);
    REQUIRE(ssh2_hash_update(&hash, "abc", 3));
    REQUIRE(ssh2_hash_final(&hash, digest.data(), digest.size()) == 1);
    CHECK(digest == unhex("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    // Ed25519 (which libssh2 compiles with curve25519) and the classic Diffie-Hellman groups fail cleanly.
    TestSession session;
    ssh2_ed25519_ctx ed{};
    const std::string message = "m", signature(64, 's');
    CHECK(ssh2_ed25519_verify(&ed, session.s, bytes(signature), signature.size(), bytes(message), 1) != 0);
    ssh2_dh_ctx dh = nullptr;
    CHECK(ssh2_dh_key_pair(&dh, nullptr, nullptr, nullptr, 256, nullptr) != 0);
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
    // A port is kept; one that is not a port leaves the address out.
    CHECK(findPublicUrl("Forwarding https://tunnel.example.org:8443/x\n") == "https://tunnel.example.org:8443");
    CHECK(findPublicUrl("https://tunnel.example.org: ready\n") == "https://tunnel.example.org");
    CHECK_FALSE(findPublicUrl("https://tunnel.example.org:99999\n").has_value());
    CHECK_FALSE(findPublicUrl("https://tunnel.example.org:0\n").has_value());
}

TEST_CASE("Tunnel: server text made safe to show on a terminal") {
    // Escape sequences (ESC, and CSI as a C1 control in UTF-8 or as a byte), other controls, DEL: replaced.
    CHECK(printableText("bad\x1b]0;owned\x07title") == "bad?]0;owned?title");
    CHECK(printableText("\xc2\x9b" "31m red \x9b" "0m \xc2\x85") == "?31m red ?0m ?");
    CHECK(printableText(std::string("nul\0del\x7f", 8)) == "nul?del?");
    CHECK(printableText("two\r\nlines\tand a tab") == "two  lines and a tab");
    // UTF-8 kept; sequences cut short, too long, of surrogates or past U+10FFFF replaced byte by byte.
    CHECK(printableText("caf\xc3\xa9 \xe2\x82\xac \xf0\x9f\x98\x80") == "caf\xc3\xa9 \xe2\x82\xac \xf0\x9f\x98\x80");
    CHECK(printableText("\xc3") == "?");
    CHECK(printableText("\xc0\xaf") == "??");
    CHECK(printableText("\xed\xa0\x80") == "???");
    CHECK(printableText("\xf4\x90\x80\x80") == "????");
    CHECK(printableText("publickey,password") == "publickey,password");
}

TEST_CASE("Tunnel: the lines of libssh2's trace") {
    auto t = readTrace("[libssh2] 0.004512 Failure Event: -11 - Unable to verify signature");
    CHECK(t.error == -11);
    CHECK(t.text == "Unable to verify signature");
    CHECK_FALSE(t.kexinit);
    CHECK(readTrace("[libssh2] 0.000123 Transport: Packet type 20 received, length=1080").kexinit);
    // What the server sent comes after the prefix and the time, where it cannot pass for a failure.
    CHECK(readTrace("[libssh2] 0.000123 Conn: Failure Event: -11 - forged").error == 0);
    CHECK(readTrace("Failure Event: -11 - forged").error == 0);
    CHECK(readTrace("[libssh2] x.1 Failure Event: -11 - forged").error == 0);
    // A code that is no number, or too large for one, is no error; the text is 256 bytes at most.
    CHECK(readTrace("[libssh2] 1.2 Failure Event: abc - x").error == 0);
    CHECK(readTrace("[libssh2] 1.2 Failure Event: -99999999999999999999 - x").error == 0);
    CHECK(readTrace("[libssh2] 1.2 Failure Event: -7 - " + std::string(1000, 'a')).text.size() == 256);
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
    CHECK(knownHostKey(file, "example.org", 22) == "");
    CHECK(rememberHostKey(file, "example.org", 22, "SHA256:aaa"));
    CHECK(rememberHostKey(file, "example.org", 2222, "SHA256:bbb"));
    CHECK(knownHostKey(file, "example.org", 22) == "SHA256:aaa");
    CHECK(knownHostKey(file, "example.org", 2222) == "SHA256:bbb");
    CHECK(rememberHostKey(file, "example.org", 22, ""));
    CHECK(knownHostKey(file, "example.org", 22) == "");
    CHECK(knownHostKey(file, "example.org", 2222) == "SHA256:bbb");
    // A value that is not a string is no key; the others still are.
    std::ofstream(file) << R"({"example.org:22": 5, "example.org:2222": "SHA256:bbb"})";
    CHECK(knownHostKey(file, "example.org", 22) == "");
    CHECK(knownHostKey(file, "example.org", 2222) == "SHA256:bbb");
    // A file that cannot be read, or is not an object, is not taken as empty, and is not written over: the keys
    // trusted before would be gone, and any key trusted in their place.
    for (const char* text : {"not json", "", R"(["example.org:2222", "SHA256:bbb"])", R"({"example.org:2222": )"}) {
        CAPTURE(text);
        std::ofstream(file) << text;
        CHECK_FALSE(knownHostKey(file, "example.org", 2222).has_value());
        CHECK_FALSE(rememberHostKey(file, "example.org", 22, "SHA256:ccc"));
        CHECK(readText(file) == text);
    }
    std::filesystem::remove_all(dir);
}

// ---------------------------------------------------------------------------------------------------------------
// A scripted SSH server on this machine (ssh_wire.hpp): the server's side of everything the tunnel does through
// libssh2, checked step by step.

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
        bool answer_shell   = true;  // localhost.run does not
        std::string request = "GET /state.json HTTP/1.0\r\n\r\n";  // sent on the forwarded connection
        int viewers         = 1;                                     // connections forwarded at once
        bool abandon        = false;  // first one the viewer leaves at once, before the answer
        // Bytes sent after the version line instead of the key exchange; the server then waits for the client to go.
        std::string raw_first;
        bool bad_signature = false;  // the host key signs something else than the exchange hash
        // Messages before the server's first KEXINIT, and between its first KEX_ECDH_REPLY and NEWKEYS.
        std::vector<std::string> before_kexinit;
        std::string in_kex;
        // Once the tunnel is on air, in place of the viewers: the scenario of a test.
        std::function<void(FakeServer&)> on_air;
        std::string version = "SSH-2.0-FakeServer_1.0";  // the server's version line
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
    // For the scenarios, on the server's thread: the client's session channel, and the window adjustments the
    // client sent, by the server's channel number.
    uint32_t session_channel = 0;
    std::map<uint32_t, uint64_t> adjusted;
    std::atomic_int keepalives{0};  // the client's, answered

    // For the scenarios.
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
    // Several messages in one write: the client reads them at once.
    void sendAll(const std::vector<std::string>& payloads) {
        for (const auto& payload : payloads) out_ += tx_.seal(payload, seq_tx_++);
        flush();
    }
    // Bytes that are no packet, in the middle of the encrypted stream.
    void sendRaw(const std::string& bytes) {
        out_ += bytes;
        flush();
    }
    // The next message, keepalives answered and window adjustments counted on the way.
    std::string next() {
        const auto until = std::chrono::steady_clock::now() + 10s;
        while (true) {
            if (auto payload = poll(50ms)) return *payload;
            expect(!quit_ && std::chrono::steady_clock::now() < until, "nothing from the client");
        }
    }
    // The same, waiting `limit` at most: nullopt when nothing came meanwhile.
    std::optional<std::string> poll(std::chrono::milliseconds limit) {
        const auto until = std::chrono::steady_clock::now() + limit;
        while (true) {
            std::string payload;
            const auto r = rx_.open(in_, seq_rx_, payload);
            expect(r != PacketCipher::Open::Bad, "bad packet from the client");
            if (r == PacketCipher::Open::Packet) {
                ++seq_rx_;
                // libssh2 opens a channel's window as it reads: nothing a scenario waits for.
                if (payload[0] == CHANNEL_WINDOW_ADJUST) {
                    Reader a(std::string_view(payload).substr(1));
                    const auto recipient = a.u32();
                    adjusted[recipient] += a.u32();
                    continue;
                }
                if (payload[0] == IGNORE_DATA) continue;
                if (payload[0] == GLOBAL_REQUEST) {
                    Reader g(std::string_view(payload).substr(1));
                    const auto name = g.string();
                    if (name == "keepalive@openssh.com" || name == "keepalive@libssh2.org") {
                        ++keepalives;
                        if (g.boolean()) send(Writer().byte(REQUEST_FAILURE).take());
                        continue;
                    }
                }
                return payload;
            }
            if (quit_) return std::nullopt;
            // The socket is read once at least, even when there is no time to wait.
            const auto left =
                std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
            std::vector<net::Wait> waits{{socket_->handle(), true, false}};
            net::waitAny(waits, static_cast<int>(std::clamp<int64_t>(left.count(), 0, 20)));
            if (waits[0].can_read) {
                expect(socket_->receive(in_, 65536) >= 0, "the client closed the connection");
            } else if (std::chrono::steady_clock::now() >= until) {
                return std::nullopt;
            }
        }
    }
    std::string nextOf(uint8_t type) {
        auto payload = next();
        expect(static_cast<uint8_t>(payload[0]) == type,
               "expected message " + std::to_string(type) + ", got " + std::to_string(uint8_t(payload[0])));
        return payload;
    }

   private:
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
        if (first) {
            for (const auto& m : script_.before_kexinit) send(m);
        }
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
                                  .string(script_.version)
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
                 .string(serverKey().sign(script_.bad_signature ? h + "x" : h))
                 .take());
        if (first && !script_.in_kex.empty()) send(script_.in_kex);
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
        out_    = script_.version + "\r\n";
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
        if (!script_.raw_first.empty()) {
            out_ += script_.raw_first;
            flush();
            next();  // the client goes, or tells
            return;
        }

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
        session_channel    = session;
        send(Writer().byte(CHANNEL_OPEN_CONFIRMATION).u32(session).u32(100).u32(1 << 20).u32(32768).take());
        const auto shell_message = nextOf(CHANNEL_REQUEST);
        Reader shell(std::string_view(shell_message).substr(1));
        shell.u32();
        expect(shell.string() == "shell", "shell");
        if (shell.boolean() && script_.answer_shell) send(Writer().byte(CHANNEL_SUCCESS).u32(session).take());
        send(Writer()
                 .byte(CHANNEL_DATA)
                 .u32(session)
                 .string("Welcome\r\nabc123.lhr.life tunneled with tls termination, https://abc123.lhr.life\r\n")
                 .take());

        if (script_.rekey) kex(false);
        if (script_.on_air) {
            script_.on_air(*this);
            untilGone();
            return;
        }

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
        untilGone();
    }

    void untilGone() {
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
                // As a server does: the client reads the end of the stream, and stops waiting for it.
                if (socket_) socket_->shutdownWrite();
                return;
            }
        }
    }

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
// The window the client gives a connection the server forwards (patched libssh2, cmake/patch_libssh2.cmake).
constexpr uint32_t FORWARDED_WINDOW = FCG_LIBSSH2_FORWARD_WINDOW;
constexpr uint16_t WEB_PORT = 26413;
constexpr uint32_t LOCALHOST_IP = 0x7F000001;

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

// A connection forwarded by the server (its channel `sender`), with `window` bytes the client may send at first: the
// client's channel number. In `client_window`, what the client lets the server send at first.
uint32_t forward(FakeServer& server, uint32_t sender, uint32_t window, uint32_t* client_window = nullptr) {
    server.send(Writer()
                    .byte(CHANNEL_OPEN)
                    .string("forwarded-tcpip")
                    .u32(sender)
                    .u32(window)
                    .u32(32768)
                    .string("localhost")
                    .u32(80)
                    .string("203.0.113.9")
                    .u32(5555)
                    .take());
    const auto confirmation = server.nextOf(CHANNEL_OPEN_CONFIRMATION);
    Reader r(std::string_view(confirmation).substr(1));
    server.expect(r.u32() == sender, "recipient");
    const auto channel = r.u32();
    if (client_window) *client_window = r.u32();
    return channel;
}

// The processor time this process used so far, in milliseconds.
int64_t ownCpuMs() {
#ifdef _WIN32
    return cpuTimeMs({static_cast<int64_t>(GetCurrentProcessId())});
#else
    return cpuTimeMs({static_cast<int64_t>(getpid())});
#endif
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
        // Without the strict exchange, IGNORE may come anywhere.
        if (!c.strict) {
            script.before_kexinit = {Writer().byte(IGNORE_DATA).string("before").take()};
            script.in_kex         = Writer().byte(IGNORE_DATA).string("during").take();
        }
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

TEST_CASE("Tunnel: a host key is trusted, or offered to the user, only once it signed the exchange") {
    const auto dir = std::filesystem::temp_directory_path() / "fcg-tunnel-signature-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto known = dir / "known_hosts.json";
    FakeServer::Script script;
    script.bad_signature = true;
    // First use: the key that did not sign is not kept (it was before its signature was checked).
    {
        FakeServer server(SSH_PORT, script);
        auto settings        = loopback("");
        settings.known_hosts = known;
        SshTunnel tunnel;
        tunnel.start(settings);
        CHECK(waitFor([&] { return tunnel.status().state == TunnelState::Failed; }));
        CHECK(tunnel.status().error.find("signature is not valid") != std::string::npos);
        CHECK(tunnel.status().new_key.empty());
        CHECK(knownHostKey(known, "127.0.0.1", SSH_PORT) == "");
    }
    // Another key known: the one that did not sign is not offered to be trusted.
    {
        REQUIRE(rememberHostKey(known, "127.0.0.1", SSH_PORT, "SHA256:anotherKey"));
        FakeServer server(SSH_PORT, script);
        auto settings        = loopback("");
        settings.known_hosts = known;
        SshTunnel tunnel;
        tunnel.start(settings);
        CHECK(waitFor([&] { return tunnel.status().state == TunnelState::Failed; }));
        CHECK(tunnel.status().new_key.empty());
        CHECK(knownHostKey(known, "127.0.0.1", SSH_PORT) == "SHA256:anotherKey");
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("Tunnel: known hosts that cannot be read, or none to keep keys in, stop it for good") {
    const auto dir = std::filesystem::temp_directory_path() / "fcg-tunnel-known-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto known = dir / "known_hosts.json";
    // A file that is not JSON: no key trusted on first use, the file left as it was.
    const std::string broken = R"({"example.org:22": "SHA256:aaa",)";
    std::ofstream(known) << broken;
    {
        FakeServer server(SSH_PORT, {});
        auto settings        = loopback("");
        settings.known_hosts = known;
        SshTunnel tunnel;
        tunnel.start(settings);
        CHECK(waitFor([&] { return tunnel.status().state == TunnelState::Failed; }));
        CHECK(tunnel.status().error.find("cannot be read") != std::string::npos);
        CHECK(tunnel.status().new_key.empty());
        CHECK(readText(known) == broken);
    }
    // Neither a key pinned nor a file to keep it in: refused, not trusted every time.
    {
        FakeServer server(SSH_PORT, {});
        SshTunnel tunnel;
        tunnel.start(loopback(""));
        CHECK(waitFor([&] { return tunnel.status().state == TunnelState::Failed; }));
        CHECK(tunnel.status().error.find("cannot be checked") != std::string::npos);
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

TEST_CASE("Tunnel: a packet length that wrapped, or a KEXINIT too short for its cookie, fails the connection") {
    // Both threw out_of_range on the tunnel's thread in the client of our own fastchess-gui had before libssh2, which
    // ended the program before any authentication.
    PacketCipher clear;
    for (const auto& raw : {std::string("\xFF\xFF\xFF\xFC", 4) + std::string(8, 'A'), clear.seal("\x14", 0),
                            clear.seal("\x14" "0123456789abcde", 0)}) {
        FakeServer::Script script;
        script.raw_first = raw;
        FakeServer server(SSH_PORT, script);
        SshTunnel tunnel;
        tunnel.start(loopback(fingerprint(serverKey().blob)));
        CHECK(waitFor([&] { return tunnel.status().state == TunnelState::Reconnecting; }));
        const auto error = tunnel.status().error;
        const bool told =
            error.find("not valid") != std::string::npos || error.find("cannot be read") != std::string::npos;
        CHECK_MESSAGE(told, error);
        CHECK(waitFor([&] { return server.done.load(); }));
    }
}

TEST_CASE("Sockets: a socket asked nothing, that hung up, does not end the wait at once") {
    // A connection whose other side closed, and this side shut its writing: poll reports a hang-up for it even when
    // asked nothing, and returned at once, again and again (a processor spent on the tunnel's loop).
    net::TcpListener listener;
    REQUIRE(listener.listen(WEB_PORT).empty());
    const auto client_fd = net::connectTo(LOCALHOST_IP, WEB_PORT);
    REQUIRE(client_fd);
    net::Socket client(*client_fd);
    const auto accepted = listener.accept(2000);
    REQUIRE(accepted);
    {
        net::Socket other(*accepted);
        CHECK(other.send("x") == 1);
    }
    std::this_thread::sleep_for(50ms);
    client.shutdownWrite();
    net::TcpListener idle_listener;
    REQUIRE(idle_listener.listen(WEB_PORT + 1).empty());
    const auto idle_fd = net::connectTo(LOCALHOST_IP, WEB_PORT + 1);
    REQUIRE(idle_fd);
    net::Socket idle(*idle_fd);
    for (const bool alone : {true, false}) {
        CAPTURE(alone);
        std::vector<net::Wait> waits{{client.handle(), false, false}};
        if (!alone) waits.push_back({idle.handle(), true, false});  // nothing comes on it
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 3; ++i) net::waitAny(waits, 100);
        CHECK(std::chrono::steady_clock::now() - start >= 250ms);
        CHECK_FALSE(waits[0].can_read);
        CHECK_FALSE(waits[0].can_write);
    }
    // Asked to read, it tells at once: the data, then the end.
    std::vector<net::Wait> waits{{client.handle(), true, false}};
    net::waitAny(waits, 1000);
    CHECK(waits[0].can_read);
}

TEST_CASE("Sockets: a connection refused on this machine is told within the time given") {
    // On Windows a refused connect takes two seconds or so unless bounded; elsewhere it is told at once.
    const auto start = std::chrono::steady_clock::now();
    CHECK_FALSE(net::connectTo(LOCALHOST_IP, WEB_PORT + 2, 200).has_value());
    CHECK(std::chrono::steady_clock::now() - start < 1500ms);
    net::TcpListener listener;
    REQUIRE(listener.listen(WEB_PORT + 2).empty());
    const auto fd = net::connectTo(LOCALHOST_IP, WEB_PORT + 2, 200);
    REQUIRE(fd);
    net::Socket socket(*fd);
    CHECK(listener.accept(2000).has_value());
}

TEST_CASE("Tunnel: a viewer's connection that hung up while the server's window is shut costs no processor") {
    // The forwarded connection's window is 0: the web server's answer waits in its socket, which hangs up once the
    // server sent the end of the request. The tunnel must wait without spinning until the window opens.
    WebServer web;
    REQUIRE(web.start(WEB_PORT).empty());
    std::atomic_bool holding{false}, release{false};
    std::string answer;
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        const auto channel = forward(server, 200, 0);
        server.send(Writer().byte(CHANNEL_DATA).u32(channel).string(script.request).take());
        server.send(Writer().byte(CHANNEL_EOF).u32(channel).take());
        std::this_thread::sleep_for(500ms);  // the web server answers and closes meanwhile
        holding = true;
        while (!release) std::this_thread::sleep_for(20ms);
        server.send(Writer().byte(CHANNEL_WINDOW_ADJUST).u32(channel).u32(1 << 20).take());
        while (true) {
            const auto m = server.next();
            Reader r(std::string_view(m).substr(1));
            server.expect(r.u32() == 200, "recipient");
            if (m[0] == CHANNEL_DATA) answer += r.string();
            if (m[0] == CHANNEL_CLOSE) break;
        }
        server.send(Writer().byte(CHANNEL_CLOSE).u32(channel).take());
    };
    FakeServer server(SSH_PORT, script);
    SshTunnel tunnel;
    tunnel.start(loopback(fingerprint(serverKey().blob)));
    REQUIRE(waitFor([&] { return holding.load() || server.done.load(); }));
    const auto cpu_before = ownCpuMs();
    std::this_thread::sleep_for(1000ms);
    const auto cpu_used = ownCpuMs() - cpu_before;
    release = true;
    CHECK_MESSAGE(cpu_used < 300, cpu_used, " ms of processor time in one second");
    CHECK(waitFor([&] { return tunnel.status().connections == 0 && !answer.empty(); }));
    tunnel.stop();
    CHECK(waitFor([&] { return server.done.load(); }));
    CHECK_MESSAGE(server.error.empty(), server.error);
    CHECK(answer.rfind("HTTP/1.0 200 OK", 0) == 0);
}

TEST_CASE("Tunnel: the strict key exchange enforced until the server's first NEWKEYS") {
    const auto ignore = Writer().byte(IGNORE_DATA).string("x").take();
    // Strict: an IGNORE before the server's KEXINIT (its sequence number 1, not 0), or after its KEX_ECDH_REPLY.
    for (const bool before : {true, false}) {
        CAPTURE(before);
        FakeServer::Script script;
        script.strict = true;
        if (before) {
            script.before_kexinit = {ignore};
        } else {
            script.in_kex = ignore;
        }
        FakeServer server(SSH_PORT, script);
        SshTunnel tunnel;
        tunnel.start(loopback(fingerprint(serverKey().blob)));
        CHECK(waitFor([&] { return tunnel.status().state == TunnelState::Reconnecting; }));
        CHECK_MESSAGE(tunnel.status().error.find("strict") != std::string::npos, tunnel.status().error);
        CHECK(waitFor([&] { return server.done.load(); }));
    }
    // Not strict: IGNORE is allowed anywhere, and libssh2 keeps a later protocol's message sent during the exchange
    // for when it is asked (a SERVICE_ACCEPT before its time), or drops it (a GLOBAL_REQUEST that wants no answer).
    for (const bool global : {true, false}) {
        CAPTURE(global);
        FakeServer::Script script;
        script.strict = false;
        if (global) {
            script.before_kexinit = {ignore, Writer().byte(GLOBAL_REQUEST).string("x").boolean(false).take()};
        } else {
            script.in_kex = Writer().byte(SERVICE_ACCEPT).string("ssh-userauth").take();
        }
        script.on_air = [](FakeServer&) {};
        FakeServer server(SSH_PORT, script);
        SshTunnel tunnel;
        tunnel.start(loopback(fingerprint(serverKey().blob)));
        CHECK(waitFor([&] { return tunnel.status().url == "https://abc123.lhr.life"; }));
        CHECK(tunnel.status().state == TunnelState::OnAir);
        tunnel.stop();
        CHECK(waitFor([&] { return server.done.load(); }));
        CHECK_MESSAGE(server.error.empty(), server.error);
    }
}

TEST_CASE("Tunnel: a server's DISCONNECT reason reaches the status without its escape sequences") {
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        const std::string reason = "bye\x1b[2J\x1b]0;pwned\x07\xc2\x9b" "0m";
        server.send(Writer().byte(DISCONNECT).u32(11).string(reason).string("").take());
    };
    FakeServer server(SSH_PORT, script);
    SshTunnel tunnel;
    tunnel.start(loopback(fingerprint(serverKey().blob)));
    CHECK(waitFor([&] { return tunnel.status().state == TunnelState::Reconnecting; }));
    const auto error = tunnel.status().error;
    CHECK(error.find("bye?[2J?]0;pwned??0m") != std::string::npos);
    CHECK(error.find('\x1b') == std::string::npos);
}

TEST_CASE("Tunnel: a server that sends what is never taken makes the connection end, not memory grow") {
    // Replies to requests never made: libssh2 keeps every message nobody asked for, until it is asked for. The
    // memory the session takes is bounded.
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        try {
            for (int i = 0; i < 100000; ++i) server.send(Writer().byte(REQUEST_SUCCESS).take());
        } catch (const FakeServer::Failure&) {
            // The client went before the end.
        }
    };
    FakeServer server(SSH_PORT, script);
    auto settings       = loopback(fingerprint(serverKey().blob));
    settings.max_memory = 1 << 20;
    SshTunnel tunnel;
    tunnel.start(settings);
    CHECK(waitFor([&] { return tunnel.status().state == TunnelState::Reconnecting; }, 30s));
    CHECK_MESSAGE(tunnel.status().error.find("memory") != std::string::npos, tunnel.status().error);
}

TEST_CASE("Tunnel: a server that asks for answers while it keeps a key exchange open is left") {
    // libssh2 answers them as they come; the exchange that never ends is the server that stopped answering.
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        server.send(Writer()
                        .byte(KEXINIT)
                        .raw(randomBytes(16))
                        .string("curve25519-sha256")
                        .string("rsa-sha2-512")
                        .string(script.ciphers)
                        .string(script.ciphers)
                        .string(script.macs)
                        .string(script.macs)
                        .string("none")
                        .string("none")
                        .string("")
                        .string("")
                        .boolean(false)
                        .u32(0)
                        .take());
        server.nextOf(KEXINIT);
        server.nextOf(KEX_ECDH_INIT);
        try {
            for (int i = 0; i < 2000; ++i) {
                server.send(Writer().byte(GLOBAL_REQUEST).string("x").boolean(true).take());
            }
        } catch (const FakeServer::Failure&) {
            // The client went before the end.
        }
    };
    FakeServer server(SSH_PORT, script);
    auto settings       = loopback(fingerprint(serverKey().blob));
    settings.max_memory = 1 << 20;
    settings.keepalive  = 1s;
    SshTunnel tunnel;
    tunnel.start(settings);
    CHECK(waitFor([&] { return tunnel.status().state == TunnelState::Reconnecting; }, 30s));
    const auto error = tunnel.status().error;
    const bool left  = error.find("stopped answering") != std::string::npos || error.find("memory") != std::string::npos;
    CHECK_MESSAGE(left, error);
}

namespace {

// A forwarded connection opened by the scenario's server, its channel `sender`.
void openForwarded(FakeServer& server, uint32_t sender) {
    server.send(Writer()
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

}  // namespace

TEST_CASE("Tunnel: 40 viewers' connections relayed at once, 40 more waiting, 40 in libssh2's queue, the next refused") {
    // A web server that takes connections and never answers: the relayed ones stay.
    net::TcpListener silent;
    REQUIRE(silent.listen(WEB_PORT).empty());
    constexpr uint32_t MAX_CHANNELS = 40;
    std::atomic_bool relayed{false}, finished{false};
    std::atomic_int confirmed{0};
    std::set<uint32_t> waiting, refused, closed;
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        for (uint32_t i = 0; i < MAX_CHANNELS; ++i) openForwarded(server, 1000 + i);
        for (uint32_t i = 0; i < MAX_CHANNELS; ++i) {
            const auto m = server.next();
            confirmed += m[0] == CHANNEL_OPEN_CONFIRMATION ? 1 : 0;
        }
        while (!relayed) std::this_thread::sleep_for(20ms);
        // As many more wait their turn, confirmed, and as many more in libssh2's queue, confirmed too (the tunnel
        // takes none of them meanwhile, nor closes them). Beyond: refused, by libssh2, before any window is given.
        for (uint32_t i = 0; i < 2 * MAX_CHANNELS + 2; ++i) openForwarded(server, 2000 + i);
        std::map<uint32_t, uint32_t> channel;
        const auto until = std::chrono::steady_clock::now() + 2s;
        while (std::chrono::steady_clock::now() < until) {
            const auto m = server.poll(50ms);
            if (!m) continue;
            Reader r(std::string_view(*m).substr(1));
            const auto recipient = r.u32();
            if ((*m)[0] == CHANNEL_OPEN_CONFIRMATION) {
                channel[recipient] = r.u32();
                waiting.insert(recipient);
            } else if ((*m)[0] == CHANNEL_OPEN_FAILURE) {
                refused.insert(recipient);
            } else if ((*m)[0] == CHANNEL_CLOSE) {
                closed.insert(recipient);
                server.send(Writer().byte(CHANNEL_CLOSE).u32(channel[recipient]).take());
            } else {
                server.expect((*m)[0] == CHANNEL_EOF, "unexpected message " + std::to_string(uint8_t((*m)[0])));
            }
        }
        finished = true;
    };
    FakeServer server(SSH_PORT, script);
    SshTunnel tunnel;
    tunnel.start(loopback(fingerprint(serverKey().blob)));
    CHECK(waitFor([&] { return tunnel.status().connections == static_cast<int>(MAX_CHANNELS); }));
    relayed = true;
    REQUIRE(waitFor([&] { return finished.load() || server.done.load(); }));
    CHECK(confirmed == static_cast<int>(MAX_CHANNELS));
    // The first 40 wait. Of the others, 40 at most wait in libssh2's queue (any of them: those that came faster than
    // the tunnel took the first ones were refused meanwhile); the rest are refused. None is closed.
    for (uint32_t i = 0; i < 2 * MAX_CHANNELS + 2; ++i) {
        CAPTURE(i);
        const auto sender = 2000 + i;
        if (i < MAX_CHANNELS) {
            CHECK(waiting.count(sender) == 1);
        } else {
            CHECK(waiting.count(sender) + refused.count(sender) == 1);
        }
    }
    CHECK(waiting.size() <= 2 * MAX_CHANNELS);
    CHECK(refused.size() >= 2);
    CHECK(closed.empty());
    CHECK(tunnel.status().connections == static_cast<int>(MAX_CHANNELS));
    tunnel.stop();
    CHECK(waitFor([&] { return server.done.load(); }));
    CHECK_MESSAGE(server.error.empty(), server.error);
}

TEST_CASE("Tunnel: viewers' connections the server never closes are forgotten, and do not keep the next out") {
    // Closed on our side, never by the server: libssh2 lets them go once its wait reads something else (a late
    // CLOSE would name a number it does not give again), and they count only while it waits, for a while at most
    // (another call may read what comes instead). The next viewers are served.
    WebServer web;
    REQUIRE(web.start(WEB_PORT).empty());
    constexpr uint32_t MAX_CHANNELS = 40;
    std::atomic_int answered_later{0};
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        // Each round answered and closed by the client; never by the server.
        for (const uint32_t base : {1000u, 2000u, 3000u}) {
            for (uint32_t i = 0; i < MAX_CHANNELS; ++i) openForwarded(server, base + i);
            uint32_t closed = 0, answered = 0;
            while (closed < MAX_CHANNELS) {
                const auto m = server.next();
                server.expect(m[0] != CHANNEL_OPEN_FAILURE, "a viewer refused");
                Reader r(std::string_view(m).substr(1));
                if (m[0] == CHANNEL_OPEN_CONFIRMATION) {
                    r.u32();
                    server.send(Writer().byte(CHANNEL_DATA).u32(r.u32()).string(script.request).take());
                }
                answered += m[0] == CHANNEL_DATA ? 1 : 0;
                closed += m[0] == CHANNEL_CLOSE ? 1 : 0;
            }
            if (base != 1000) answered_later += answered > 0 ? 1 : 0;
        }
    };
    FakeServer server(SSH_PORT, script);
    auto settings          = loopback(fingerprint(serverKey().blob));
    settings.close_timeout = 1s;
    SshTunnel tunnel;
    tunnel.start(settings);
    CHECK(waitFor([&] { return server.done.load() || answered_later == 2; }, 30s));
    tunnel.stop();
    CHECK(waitFor([&] { return server.done.load(); }));
    CHECK_MESSAGE(server.error.empty(), server.error);
    CHECK(answered_later == 2);
}

namespace {

std::string channelData(uint32_t channel, std::string_view bytes) {
    return Writer().byte(CHANNEL_DATA).u32(channel).string(bytes).take();
}

// The server closes the forwarded connection (its channel `sender`, the client's `channel`), and waits for the
// client's close.
void closeForwarded(FakeServer& server, uint32_t sender, uint32_t channel) {
    server.send(Writer().byte(CHANNEL_CLOSE).u32(channel).take());
    while (true) {
        const auto m = server.next();
        Reader r(std::string_view(m).substr(1));
        server.expect(r.u32() == sender, "recipient");
        if (m[0] == CHANNEL_CLOSE) return;
    }
}

// A web server of one connection: reads all it is sent, and sends `answer` while it reads, or only once the request
// ended.
class Sink {
   public:
    Sink(std::string answer, bool while_reading) : answer_(std::move(answer)), while_reading_(while_reading) {
        REQUIRE(listener_.listen(WEB_PORT).empty());
        thread_ = std::thread([this] { run(); });
    }
    ~Sink() {
        quit_ = true;
        thread_.join();
    }
    std::atomic<std::size_t> received{0};
    // What it received.
    std::string content() {
        std::lock_guard<std::mutex> lock(mutex_);
        return content_;
    }

   private:
    void run() {
        std::optional<net::Handle> fd;
        while (!fd && !quit_) fd = listener_.accept(50);
        if (!fd) return;
        net::Socket socket(*fd);
        std::size_t sent = 0;
        bool ended       = false;
        std::string in;
        while (!quit_) {
            const bool sending = (while_reading_ || ended) && sent < answer_.size();
            if (ended && !sending) break;
            std::vector<net::Wait> waits{{socket.handle(), !ended, sending}};
            net::waitAny(waits, 50);
            if (!ended && waits[0].can_read) {
                in.clear();
                const auto n = socket.receive(in, 1 << 16);
                if (n < 0) {
                    ended = true;
                } else {
                    std::lock_guard<std::mutex> lock(mutex_);
                    content_ += in;
                    received += static_cast<std::size_t>(n);
                }
            }
            if (sending && waits[0].can_write) {
                const auto n = socket.send(std::string_view(answer_).substr(sent, 1 << 16));
                if (n < 0) return;
                sent += static_cast<std::size_t>(n);
            }
        }
        // Kept open until the test ends: the server closes the connection once it has the whole answer.
        while (!quit_) std::this_thread::sleep_for(20ms);
    }

    std::string answer_;
    bool while_reading_;
    std::mutex mutex_;
    std::string content_;
    net::TcpListener listener_;
    std::thread thread_;
    std::atomic_bool quit_{false};
};

}  // namespace

TEST_CASE("Tunnel: viewers who send more than the web server takes are let go, each alone, and the tunnel stays") {
    // libssh2 gave every forwarded connection a 2 MiB window, which the server may fill before it is read: 40 relayed
    // and 40 waiting, 160 MiB. 16 uploads waiting their turn behind viewers of a web server that does not read went
    // past the session's memory, and the connection ended, again and again. Every connection is read now, whether the
    // web server takes what it sends or not, and let go once the tunnel keeps more than its budget for it: libssh2's
    // memory stays far below the 4 MiB allowed here.
    net::TcpListener silent;  // takes the connections, never reads them
    REQUIRE(silent.listen(WEB_PORT).empty());
    constexpr uint32_t RELAYED = 40, WAITING = 40, UPLOADERS = 4;
    std::atomic_bool relayed{false}, finished{false};
    std::vector<uint32_t> windows;
    std::set<uint32_t> closed;
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        // By the server's channel number: the client's, the window it opened with, and what was sent on it.
        std::map<uint32_t, uint32_t> channel;
        std::map<uint32_t, uint64_t> window, sent;
        const auto opened = [&](uint32_t sender, uint32_t client_channel, uint32_t client_window) {
            channel[sender] = client_channel;
            window[sender]  = client_window;
            windows.push_back(client_window);
        };
        for (uint32_t i = 0; i < RELAYED; ++i) {
            uint32_t client_window    = 0;
            const auto client_channel = forward(server, 1000 + i, 1 << 20, &client_window);
            opened(1000 + i, client_channel, client_window);
        }
        while (!relayed) std::this_thread::sleep_for(20ms);
        for (uint32_t i = 0; i < WAITING; ++i) openForwarded(server, 2000 + i);
        // The uploaders (4 of the viewers relayed, and all those waiting) send all their window lets them, and more
        // as it opens, until they are let go.
        const auto uploader = [&](uint32_t sender) { return sender < 1000 + UPLOADERS || sender >= 2000; };
        const std::string chunk(32768, 'u');
        const auto until = std::chrono::steady_clock::now() + 40s;
        auto last_close  = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() < until) {
            bool sending = false;
            for (const auto& [sender, client_channel] : channel) {
                if (!uploader(sender) || closed.count(sender) != 0) continue;
                const auto room = window[sender] + server.adjusted[sender] - sent[sender];
                if (room == 0) continue;
                const auto n = std::min<uint64_t>(room, chunk.size());
                server.send(channelData(client_channel, std::string_view(chunk).substr(0, n)));
                sent[sender] += n;
                sending = true;
            }
            const auto m = server.poll(sending ? 0ms : 50ms);
            if (!m) {
                // Every uploader let go, and nothing more for a second: done.
                if (closed.size() >= UPLOADERS + WAITING && std::chrono::steady_clock::now() - last_close > 1s) break;
                continue;
            }
            Reader r(std::string_view(*m).substr(1));
            const auto recipient = r.u32();
            if ((*m)[0] == CHANNEL_OPEN_CONFIRMATION) {
                const auto client_channel = r.u32();
                opened(recipient, client_channel, r.u32());
            } else if ((*m)[0] == CHANNEL_CLOSE) {
                closed.insert(recipient);
                last_close = std::chrono::steady_clock::now();
                server.send(Writer().byte(CHANNEL_CLOSE).u32(channel[recipient]).take());
            } else {
                server.expect((*m)[0] == CHANNEL_EOF, "unexpected message " + std::to_string(uint8_t((*m)[0])));
            }
        }
        finished = true;
    };
    FakeServer server(SSH_PORT, script);
    auto settings       = loopback(fingerprint(serverKey().blob));
    settings.max_memory = 4 << 20;
    SshTunnel tunnel;
    tunnel.start(settings);
    CHECK(waitFor([&] { return tunnel.status().connections == static_cast<int>(RELAYED); }));
    relayed = true;
    REQUIRE(waitFor([&] { return finished.load() || server.done.load(); }, 60s));
    const auto status = tunnel.status();
    CHECK(status.state == TunnelState::OnAir);
    CHECK_MESSAGE(status.error.empty(), status.error);
    CHECK(status.connections == static_cast<int>(RELAYED - UPLOADERS));
    // Every forwarded connection's window: patched libssh2's.
    CHECK(windows.size() == RELAYED + WAITING);
    CHECK(std::all_of(windows.begin(), windows.end(), [](uint32_t w) { return w == FORWARDED_WINDOW; }));
    // Let go: the uploaders, and only them.
    std::set<uint32_t> uploaders;
    for (uint32_t i = 0; i < UPLOADERS; ++i) uploaders.insert(1000 + i);
    for (uint32_t i = 0; i < WAITING; ++i) uploaders.insert(2000 + i);
    CHECK(closed == uploaders);
    tunnel.stop();
    CHECK(waitFor([&] { return server.done.load(); }));
    CHECK_MESSAGE(server.error.empty(), server.error);
}

namespace {

// What a viewer uploads on the forwarded connection `sender` (the client's `channel`, which opened with `window`, and
// on which `before` bytes went already): packets of `size` bytes within its window, until the client closes it or
// `count` bytes went; the close is answered. Packets of one byte go 256 at a time; larger ones one at a time, a few
// milliseconds apart, so that little is on its way when the client closes. `sent`: the bytes that went, counted as
// they go.
void upload(FakeServer& server, uint32_t sender, uint32_t channel, uint64_t window, uint64_t before, std::size_t size,
            std::size_t count, std::set<uint32_t>& closed, std::atomic<std::size_t>& sent) {
    const std::string chunk(size, 'u');
    const auto until = std::chrono::steady_clock::now() + 60s;
    while (sent < count && closed.count(sender) == 0 && std::chrono::steady_clock::now() < until) {
        const auto room = window + server.adjusted[sender] - before - sent;
        if (size == 1) {
            for (std::size_t i = 0; i < std::min<uint64_t>(room, 256) && sent < count; ++i, ++sent) {
                server.send(channelData(channel, chunk));
            }
        } else if (room > 0) {
            const auto n = std::min<uint64_t>({room, size, count - sent});
            server.send(channelData(channel, std::string_view(chunk).substr(0, n)));
            sent += n;
        }
        auto wait = room == 0 ? 50ms : size == 1 ? 0ms : 2ms;
        while (const auto m = server.poll(wait)) {
            Reader r(std::string_view(*m).substr(1));
            const auto recipient = r.u32();
            if ((*m)[0] == CHANNEL_CLOSE) {
                closed.insert(recipient);
                if (recipient == sender) server.send(Writer().byte(CHANNEL_CLOSE).u32(channel).take());
            } else {
                server.expect((*m)[0] == CHANNEL_EOF, "unexpected message " + std::to_string(uint8_t((*m)[0])));
            }
            wait = 0ms;
        }
    }
}

// Anything else the client closes, for a second.
void closedMeanwhile(FakeServer& server, std::set<uint32_t>& closed) {
    const auto until = std::chrono::steady_clock::now() + 1s;
    while (std::chrono::steady_clock::now() < until) {
        const auto m = server.poll(50ms);
        if (m && (*m)[0] == CHANNEL_CLOSE) closed.insert(Reader(std::string_view(*m).substr(1)).u32());
    }
}

constexpr std::size_t BUDGET = 128 * 1024;  // the tunnel's VIEWER_BUDGET

}  // namespace

TEST_CASE("Tunnel: one-byte packets on a connection waiting its turn: the viewer is let go, alone, the tunnel stays") {
    // libssh2 keeps each packet it received apart until it is read (some 80 bytes of memory for one byte), and a
    // connection waiting its turn was not read at all: 53,000 one-byte packets, well within its window, ended a
    // session of 4 MiB. Each is read into the tunnel's own buffers now, a byte for a byte, until its budget.
    net::TcpListener silent;  // takes the connections, never reads them: the relayed ones stay
    REQUIRE(silent.listen(WEB_PORT).empty());
    constexpr uint32_t RELAYED = 40;
    std::atomic_bool relayed{false}, finished{false};
    std::atomic<std::size_t> sent{0};
    std::set<uint32_t> closed;
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        for (uint32_t i = 0; i < RELAYED; ++i) forward(server, 1000 + i, 1 << 20);
        while (!relayed) std::this_thread::sleep_for(20ms);
        // Two wait their turn: a viewer's request, and one-byte packets.
        const auto viewer = forward(server, 2000, 1 << 20);
        server.send(channelData(viewer, script.request));
        uint32_t window     = 0;
        const auto offender = forward(server, 2001, 1 << 20, &window);
        upload(server, 2001, offender, window, 0, 1, 1000000, closed, sent);
        closedMeanwhile(server, closed);
        finished = true;
    };
    FakeServer server(SSH_PORT, script);
    auto settings       = loopback(fingerprint(serverKey().blob));
    settings.max_memory = 4 << 20;
    SshTunnel tunnel;
    tunnel.start(settings);
    CHECK(waitFor([&] { return tunnel.status().connections == static_cast<int>(RELAYED); }));
    relayed = true;
    REQUIRE(waitFor([&] { return finished.load() || server.done.load(); }, 90s));
    MESSAGE(sent.load(), " one-byte packets went");
    const auto status = tunnel.status();
    CHECK(status.state == TunnelState::OnAir);
    CHECK_MESSAGE(status.error.empty(), status.error);
    CHECK(status.connections == static_cast<int>(RELAYED));
    CHECK(closed == std::set<uint32_t>{2001});
    CHECK(sent > BUDGET);
    tunnel.stop();
    CHECK(waitFor([&] { return server.done.load(); }));
    CHECK_MESSAGE(server.error.empty(), server.error);
}

TEST_CASE("Tunnel: one-byte packets on a relayed connection the web server does not read: let go, the tunnel stays") {
    // Once the sockets between the tunnel and the web server were full, the relayed connection was read no more, and
    // libssh2 kept its one-byte packets until it held more than the budget for it: 10 MiB of memory, past what the
    // session may hold. The sockets are filled first, in large packets: as much as a first viewer's upload took
    // before it was let go, its budget and a little more left out.
    net::TcpListener silent;  // takes the connections, never reads them
    REQUIRE(silent.listen(WEB_PORT).empty());
    std::atomic_bool finished{false};
    std::atomic<std::size_t> first{0}, filled{0}, sent{0};
    std::set<uint32_t> closed;
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        uint32_t window     = 0;
        const auto measured = forward(server, 200, 1 << 20, &window);
        upload(server, 200, measured, window, 0, 32768, std::size_t(64) << 20, closed, first);
        const auto offender = forward(server, 201, 1 << 20, &window);
        const auto fill     = first > BUDGET + (64 << 10) ? first - BUDGET - (64 << 10) : 0;
        upload(server, 201, offender, window, 0, 32768, fill, closed, filled);
        upload(server, 201, offender, window, filled, 1, 3000000, closed, sent);
        closedMeanwhile(server, closed);
        finished = true;
    };
    FakeServer server(SSH_PORT, script);
    auto settings       = loopback(fingerprint(serverKey().blob));
    settings.max_memory = 4 << 20;
    SshTunnel tunnel;
    tunnel.start(settings);
    REQUIRE(waitFor([&] { return finished.load() || server.done.load(); }, 120s));
    MESSAGE(first.load(), " bytes on the first connection; ", filled.load(), " bytes, then ", sent.load(),
            " one-byte packets on the second");
    const auto status = tunnel.status();
    CHECK(status.state == TunnelState::OnAir);
    CHECK_MESSAGE(status.error.empty(), status.error);
    CHECK(status.connections == 0);
    CHECK(closed == std::set<uint32_t>{200, 201});
    CHECK(sent > 0);  // let go for its one-byte packets, not before
    tunnel.stop();
    CHECK(waitFor([&] { return server.done.load(); }));
    CHECK_MESSAGE(server.error.empty(), server.error);
}

TEST_CASE("Tunnel: a read whose window adjustment did not go at once is called again, the connection not frozen") {
    // libssh2 sends nothing else until a packet it started is sent, by the same call. A viewer's read starts with an
    // adjustment of its window; once the end of the upload had come, the read was not called again, and the web
    // server's answer never went. Here every packet takes several sends.
    constexpr std::size_t ANSWER = 100000;
    Sink web(std::string(ANSWER, 'a'), false);
    std::atomic_bool finished{false};
    std::size_t answered = 0;
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        const auto channel = forward(server, 200, 1 << 20);
        const std::string upload(30000, 'u');
        server.send(channelData(channel, upload));
        server.send(channelData(channel, upload));
        server.send(Writer().byte(CHANNEL_EOF).u32(channel).take());
        while (answered < ANSWER) {
            const auto m = server.next();
            Reader r(std::string_view(m).substr(1));
            server.expect(r.u32() == 200, "recipient");
            if (m[0] == CHANNEL_DATA) answered += r.string().size();
            server.expect(m[0] != CHANNEL_CLOSE, "closed before the whole answer");
        }
        closeForwarded(server, 200, channel);
        finished = true;
    };
    FakeServer server(SSH_PORT, script);
    auto settings       = loopback(fingerprint(serverKey().blob));
    settings.send_limit = 16;
    SshTunnel tunnel;
    tunnel.start(settings);
    CHECK(waitFor([&] { return finished.load() || server.done.load(); }, 30s));
    CHECK(answered == ANSWER);
    CHECK(web.received == 60000);
    CHECK(tunnel.status().state == TunnelState::OnAir);
    tunnel.stop();
    CHECK(waitFor([&] { return server.done.load(); }));
    CHECK_MESSAGE(server.error.empty(), server.error);
}

TEST_CASE("Tunnel: a viewer's upload while the server does not read costs no processor, and all goes once it reads") {
    // The web server answers while it reads, and the server reads nothing for a while: the connection is full, and
    // the viewer's reads cannot send the window adjustment they start with. They must not be given up on, nor tried
    // again and again without waiting. The answer is more than sockets hold while they are not read.
    constexpr std::size_t UPLOAD = 768 * 1024, ANSWER = std::size_t(16) << 20;
    Sink web(std::string(ANSWER, 'a'), true);
    std::atomic_bool holding{false}, release{false}, finished{false};
    std::size_t answered = 0;
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        uint32_t window    = 0;
        const auto channel = forward(server, 200, 0x7FFFFFFF, &window);
        std::size_t sent   = 0;
        const std::string chunk(32768, 'u');
        const auto sendWhatFits = [&] {
            while (sent < UPLOAD && window + server.adjusted[200] > sent) {
                const auto n =
                    std::min<std::size_t>({window + server.adjusted[200] - sent, chunk.size(), UPLOAD - sent});
                server.send(channelData(channel, std::string_view(chunk).substr(0, n)));
                sent += n;
            }
        };
        std::this_thread::sleep_for(500ms);  // the answer fills the connection
        sendWhatFits();
        holding = true;
        while (!release) std::this_thread::sleep_for(20ms);
        const auto until = std::chrono::steady_clock::now() + 20s;
        while (std::chrono::steady_clock::now() < until && (answered < ANSWER || web.received < UPLOAD)) {
            sendWhatFits();
            const auto m = server.poll(50ms);
            if (!m) continue;
            Reader r(std::string_view(*m).substr(1));
            server.expect(r.u32() == 200, "recipient");
            if ((*m)[0] == CHANNEL_DATA) answered += r.string().size();
            server.expect((*m)[0] != CHANNEL_CLOSE, "closed before the whole answer");
        }
        closeForwarded(server, 200, channel);
        finished = true;
    };
    FakeServer server(SSH_PORT, script);
    SshTunnel tunnel;
    tunnel.start(loopback(fingerprint(serverKey().blob)));
    REQUIRE(waitFor([&] { return holding.load() || server.done.load(); }));
    std::this_thread::sleep_for(100ms);  // the upload read as far as it can be
    const auto cpu_before = ownCpuMs();
    std::this_thread::sleep_for(1000ms);
    const auto cpu_used = ownCpuMs() - cpu_before;
    release             = true;
    CHECK_MESSAGE(cpu_used < 300, cpu_used, " ms of processor time in one second");
    CHECK(waitFor([&] { return finished.load() || server.done.load(); }, 30s));
    CHECK(answered == ANSWER);
    CHECK(web.received == UPLOAD);
    CHECK(tunnel.status().state == TunnelState::OnAir);
    tunnel.stop();
    CHECK(waitFor([&] { return server.done.load(); }));
    CHECK_MESSAGE(server.error.empty(), server.error);
}

TEST_CASE("Tunnel: a viewer's bytes, merged by libssh2 as they wait, reach the web server whole and in order") {
    // libssh2 appends a channel's small packets to the one it holds for it (cmake/patch_libssh2.cmake). Numbered bytes
    // in packets of 1 to 37 bytes, three windows of them: read as they come, part of a packet at a time, with others
    // appended meanwhile, then held while the server reads nothing (the web server's answer fills the connection),
    // and read again once it does. The web server has every byte, in order.
    Sink web(std::string(std::size_t(16) << 20, 'a'), true);
    constexpr std::size_t TOTAL = 3 * FORWARDED_WINDOW;
    std::string upload(TOTAL, '\0');
    for (std::size_t i = 0; i < TOTAL; ++i) upload[i] = static_cast<char>(i % 251);
    std::atomic_bool finished{false};
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        uint32_t window    = 0;
        const auto channel = forward(server, 200, 0x7FFFFFFF, &window);
        std::size_t sent = 0, size = 1;
        const auto until = std::chrono::steady_clock::now() + 60s;
        while (std::chrono::steady_clock::now() < until && web.received < TOTAL) {
            const auto room = window + server.adjusted[200] - sent;
            if (sent < TOTAL && room > 0) {
                const auto n = std::min<std::size_t>({size, room, TOTAL - sent});
                server.send(channelData(channel, std::string_view(upload).substr(sent, n)));
                sent += n;
                size = size % 37 + 1;
            } else {
                server.poll(5ms);  // the answer read, the window adjusted
            }
        }
        finished = true;
    };
    FakeServer server(SSH_PORT, script);
    SshTunnel tunnel;
    tunnel.start(loopback(fingerprint(serverKey().blob)));
    REQUIRE(waitFor([&] { return finished.load() || server.done.load(); }, 90s));
    const auto got = web.content();
    std::size_t same = 0;
    while (same < std::min(got.size(), upload.size()) && got[same] == upload[same]) ++same;
    CHECK_MESSAGE(got == upload, got.size(), " bytes, the first ", same, " as sent");
    CHECK(tunnel.status().state == TunnelState::OnAir);
    tunnel.stop();
    CHECK(waitFor([&] { return server.done.load(); }));
    CHECK_MESSAGE(server.error.empty(), server.error);
}

TEST_CASE("Tunnel: the session's text and error text, come together, are read in the order they came") {
    // libssh2 appends a packet only to the last one it holds for the channel, and only of the same stream: the
    // session's text and error text, read as one, keep their order, and so does the address in them.
    std::atomic_bool finished{false};
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        const auto text = [&](std::string_view s) {
            return Writer().byte(CHANNEL_DATA).u32(server.session_channel).string(s).take();
        };
        const auto error = [&](std::string_view s) {
            return Writer().byte(CHANNEL_EXTENDED_DATA).u32(server.session_channel).u32(1).string(s).take();
        };
        server.sendAll({text("in.lhr.life tunneled, https://"), error("in"), error("ter"), text("le"), text("av"),
                        error("ed"), error(".lh"), text("r.life\r\n")});
        while (!finished) server.poll(50ms);
    };
    FakeServer server(SSH_PORT, script);
    SshTunnel tunnel;
    tunnel.start(loopback(fingerprint(serverKey().blob)));
    CHECK(waitFor([&] { return tunnel.status().url == "https://interleaved.lhr.life"; }));
    CHECK_MESSAGE(tunnel.status().url == "https://interleaved.lhr.life", tunnel.status().url);
    finished = true;
    tunnel.stop();
    CHECK(waitFor([&] { return server.done.load(); }));
    CHECK_MESSAGE(server.error.empty(), server.error);
}

TEST_CASE("Tunnel: one-byte packets while the server reads nothing: libssh2 holds their bytes, not a block for each") {
    // Once a viewer's window runs low, its read starts with the window's adjustment, and libssh2 hands nothing over
    // until that went. While the server reads nothing (the web server's answer fills the connection), a viewer could
    // send one-byte packets within the rest of its window (2 MiB then), which libssh2 kept as a block of memory each:
    // some 116 bytes for one, 174 MiB, and the session ended. libssh2 now appends such a packet to the one it holds
    // for the channel (cmake/patch_libssh2.cmake): what it keeps is the bytes, within the 1 MiB allowed here (what a
    // window of one-byte packets took before, 11 MiB, would end the session).
    Sink web(std::string(std::size_t(16) << 20, 'a'), true);
    std::atomic_bool finished{false};
    std::atomic<std::size_t> sent{0};
    std::atomic<uint64_t> unread{0}, adjusted{0};
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        uint32_t window    = 0;
        const auto channel = forward(server, 200, 0x7FFFFFFF, &window);
        std::this_thread::sleep_for(500ms);  // the answer fills the connection
        for (; sent < window; ++sent) server.send(channelData(channel, "u"));
        // What the web server did not have yet: libssh2 holds it, or the tunnel.
        unread = sent - web.received;
        // The server reads again, and the window's adjustment comes.
        const auto until = std::chrono::steady_clock::now() + 20s;
        while (std::chrono::steady_clock::now() < until && server.adjusted[200] == 0) server.poll(50ms);
        adjusted = server.adjusted[200];
        finished = true;
    };
    FakeServer server(SSH_PORT, script);
    auto settings       = loopback(fingerprint(serverKey().blob));
    settings.max_memory = 1 << 20;
    SshTunnel tunnel;
    tunnel.start(settings);
    REQUIRE(waitFor([&] { return finished.load() || server.done.load(); }, 90s));
    MESSAGE(sent.load(), " one-byte packets went, ", unread.load(), " of them held while the server read nothing");
    const auto status = tunnel.status();
    CHECK(status.state == TunnelState::OnAir);
    CHECK_MESSAGE(status.error.empty(), status.error);
    CHECK(sent == FORWARDED_WINDOW);
    CHECK(unread > FORWARDED_WINDOW / 2);  // the adjustment could not go: most of the window waited
    CHECK(adjusted > 0);
    tunnel.stop();
    CHECK(waitFor([&] { return server.done.load(); }));
    CHECK_MESSAGE(server.error.empty(), server.error);
}

namespace {

// A web server of many connections: reads all they send, and sends `answer` to the first one meanwhile.
class FirstAnswered {
   public:
    explicit FirstAnswered(std::size_t answer) : answer_(answer, 'a') {
        REQUIRE(listener_.listen(WEB_PORT).empty());
        thread_ = std::thread([this] { run(); });
    }
    ~FirstAnswered() {
        quit_ = true;
        thread_.join();
    }

   private:
    struct Connection {
        std::unique_ptr<net::Socket> socket;
        bool ended = false;
    };
    void run() {
        std::vector<Connection> connections;
        std::size_t sent = 0;
        std::string in;
        while (!quit_) {
            if (auto fd = listener_.accept(0)) connections.push_back({std::make_unique<net::Socket>(*fd)});
            if (connections.empty()) {
                std::this_thread::sleep_for(5ms);
                continue;
            }
            std::vector<net::Wait> waits;
            for (std::size_t i = 0; i < connections.size(); ++i) {
                waits.push_back(
                    {connections[i].socket->handle(), !connections[i].ended, i == 0 && sent < answer_.size()});
            }
            net::waitAny(waits, 5);
            for (std::size_t i = 0; i < connections.size(); ++i) {
                auto& c = connections[i];
                if (!c.ended && waits[i].can_read) {
                    in.clear();
                    c.ended = c.socket->receive(in, 1 << 16) < 0;
                }
                if (i == 0 && waits[i].can_write && sent < answer_.size()) {
                    const auto n = c.socket->send(std::string_view(answer_).substr(sent, 1 << 16));
                    if (n > 0) sent += static_cast<std::size_t>(n);
                }
            }
        }
    }

    std::string answer_;
    net::TcpListener listener_;
    std::thread thread_;
    std::atomic_bool quit_{false};
};

}  // namespace

TEST_CASE("Tunnel: every viewer's window filled while the server reads nothing stays within the session's memory") {
    // While the server reads nothing (the web server's answer to a first viewer fills the connection), every viewer's
    // read waits for its window's adjustment, and libssh2 holds what the server sends meanwhile, up to the window. It
    // was 2 MiB: 24 viewers sending theirs in large packets ended the session. 128 KiB now (cmake/patch_libssh2.cmake):
    // 39 viewers relayed, 40 waiting their turn and 40 in libssh2's queue (confirmed, not taken by the tunnel) send all
    // their window can take, the tunnel stays.
    FirstAnswered web(std::size_t(16) << 20);
    constexpr uint32_t RELAYED = 39, WAITING = 40, QUEUED = 40;
    std::atomic_bool finished{false};
    std::atomic<std::size_t> sent{0}, recovered{0};
    std::vector<uint32_t> windows;
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        forward(server, 100, 0x7FFFFFFF);  // the first viewer, answered at once
        // The others one at a time, each confirmed among the answer's packets. libssh2 refuses those that come
        // faster than the tunnel takes them (QUEUE): opened again a moment later.
        std::map<uint32_t, uint32_t> channel;
        for (uint32_t i = 0; i < RELAYED + WAITING + QUEUED; ++i) {
            const auto sender = i < RELAYED ? 200 + i : i < RELAYED + WAITING ? 300 + i - RELAYED : 400 + i - RELAYED - WAITING;
            openForwarded(server, sender);
            while (channel.count(sender) == 0) {
                const auto m = server.next();
                if (m[0] == CHANNEL_OPEN_FAILURE) {
                    std::this_thread::sleep_for(20ms);
                    openForwarded(server, sender);
                }
                if (m[0] != CHANNEL_OPEN_CONFIRMATION) continue;
                Reader r(std::string_view(m).substr(1));
                server.expect(r.u32() == sender, "recipient");
                channel[sender] = r.u32();
                windows.push_back(r.u32());
            }
        }
        std::this_thread::sleep_for(500ms);  // the answer fills the connection
        const std::string chunk(32768, 'u');
        for (std::size_t at = 0; at < windows.front(); at += chunk.size()) {
            for (const auto& [sender, client_channel] : channel) {
                server.send(channelData(client_channel, chunk));
                sent += chunk.size();
            }
        }
        // The server reads again: every viewer the tunnel took has its window adjusted, what libssh2 held for it read.
        const auto until = std::chrono::steady_clock::now() + 30s;
        while (std::chrono::steady_clock::now() < until) {
            server.poll(50ms);
            recovered = std::count_if(channel.begin(), channel.end(),
                                      [&](const auto& c) { return c.first < 400 && server.adjusted[c.first] > 0; });
            if (recovered == RELAYED + WAITING) break;
        }
        finished = true;
    };
    FakeServer server(SSH_PORT, script);
    SshTunnel tunnel;
    tunnel.start(loopback(fingerprint(serverKey().blob)));
    REQUIRE(waitFor([&] { return finished.load() || server.done.load(); }, 90s));
    MESSAGE(sent.load(), " bytes went while the server read nothing");
    const auto status = tunnel.status();
    CHECK(status.state == TunnelState::OnAir);
    CHECK_MESSAGE(status.error.empty(), status.error);
    CHECK(windows.size() == RELAYED + WAITING + QUEUED);
    CHECK(std::all_of(windows.begin(), windows.end(), [](uint32_t w) { return w == FORWARDED_WINDOW; }));
    CHECK(sent == (RELAYED + WAITING + QUEUED) * std::size_t(FORWARDED_WINDOW));
    CHECK(recovered == RELAYED + WAITING);
    tunnel.stop();
    CHECK(waitFor([&] { return server.done.load(); }));
    CHECK_MESSAGE(server.error.empty(), server.error);
}

TEST_CASE("Tunnel: a request of the server's while it reads nothing costs no processor until the answer can go") {
    // libssh2 reads nothing more until it sent its answer to a request of the server's. While the server read nothing
    // (the web server's answer fills the connection), what it sent after the request kept the socket readable, and
    // the tunnel woke at once to read it, again and again, all a processor's time.
    Sink web(std::string(std::size_t(16) << 20, 'a'), true);
    std::atomic_bool holding{false}, release{false}, finished{false}, answered{false};
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        const auto channel = forward(server, 200, 0x7FFFFFFF);
        std::this_thread::sleep_for(500ms);  // the answer fills the connection
        server.send(Writer().byte(GLOBAL_REQUEST).string("keepalive@openssh.com").boolean(true).take());
        server.send(channelData(channel, "more"));
        holding = true;
        while (!release) std::this_thread::sleep_for(20ms);
        const auto until = std::chrono::steady_clock::now() + 20s;
        while (std::chrono::steady_clock::now() < until && !answered) {
            const auto m = server.poll(50ms);
            answered     = m && (*m)[0] == REQUEST_FAILURE;
        }
        finished = true;
    };
    FakeServer server(SSH_PORT, script);
    SshTunnel tunnel;
    tunnel.start(loopback(fingerprint(serverKey().blob)));
    REQUIRE(waitFor([&] { return holding.load() || server.done.load(); }));
    std::this_thread::sleep_for(100ms);
    const auto cpu_before = ownCpuMs();
    std::this_thread::sleep_for(1000ms);
    const auto cpu_used = ownCpuMs() - cpu_before;
    release             = true;
    CHECK_MESSAGE(cpu_used < 300, cpu_used, " ms of processor time in one second");
    CHECK(waitFor([&] { return finished.load() || server.done.load(); }, 30s));
    CHECK(answered);
    CHECK(tunnel.status().state == TunnelState::OnAir);
    tunnel.stop();
    CHECK(waitFor([&] { return server.done.load(); }));
    CHECK_MESSAGE(server.error.empty(), server.error);
}

TEST_CASE("Tunnel: a keepalive that went in part is sent on, though the server talks again") {
    // libssh2 sends the rest of a keepalive only from the call that sends one, and nothing else meanwhile. The tunnel
    // made that call only while the server was silent: once it talked again, nothing went until it was silent for as
    // long again (30 s). Here every packet takes several sends, and the server talks all the time once the keepalive
    // went in part.
    WebServer web;
    REQUIRE(web.start(WEB_PORT).empty());
    std::atomic_bool finished{false};
    std::atomic_int keepalives_meanwhile{0};
    std::string answer;
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        std::this_thread::sleep_for(1500ms);  // silent: a keepalive goes, in part
        // Talking, for 3 s: the rest of the keepalive comes meanwhile (libssh2 sends it again after 2 s).
        const auto talking = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < talking) {
            server.send(Writer().byte(IGNORE_DATA).string("x").take());
            server.expect(!server.poll(200ms).has_value(), "unexpected message");
        }
        keepalives_meanwhile = server.keepalives.load();
        // And a viewer, served.
        openForwarded(server, 200);
        const auto until = std::chrono::steady_clock::now() + 10s;
        uint32_t channel = 0;
        while (std::chrono::steady_clock::now() < until) {
            server.send(Writer().byte(IGNORE_DATA).string("x").take());
            const auto m = server.poll(200ms);
            if (!m) continue;
            Reader r(std::string_view(*m).substr(1));
            server.expect(r.u32() == 200, "recipient");
            if ((*m)[0] == CHANNEL_OPEN_CONFIRMATION) {
                channel = r.u32();
                server.send(channelData(channel, script.request));
            } else if ((*m)[0] == CHANNEL_DATA) {
                answer += r.string();
            } else if ((*m)[0] == CHANNEL_CLOSE) {
                server.send(Writer().byte(CHANNEL_CLOSE).u32(channel).take());
                break;
            }
        }
        finished = true;
    };
    FakeServer server(SSH_PORT, script);
    auto settings       = loopback(fingerprint(serverKey().blob));
    settings.keepalive  = 1s;
    settings.send_limit = 40;
    SshTunnel tunnel;
    tunnel.start(settings);
    REQUIRE(waitFor([&] { return finished.load() || server.done.load(); }, 20s));
    CHECK(keepalives_meanwhile.load() == 1);
    CHECK(answer.rfind("HTTP/1.0 200 OK", 0) == 0);
    CHECK(tunnel.status().state == TunnelState::OnAir);
    tunnel.stop();
    CHECK(waitFor([&] { return server.done.load(); }));
}

namespace {

// The tunnel stopped while libssh2 has a packet half sent: the web server's answer filled the connection to the
// server, which reads nothing. Then, as asked, a request of the server's whose answer cannot go, and a viewer's
// connection whose confirmation cannot go, for which libssh2 made a channel it holds in no list. libssh2 sent nothing
// else until that packet was finished, its channels' closing included, and its session was never freed, with its
// channels, keys and the viewer's channel. No DISCONNECT can go: the server is not waited for.
void stopWhileHalfSent(bool request, bool viewer) {
    const auto left_before = tunnelMemoryLeftBehind();
    Sink web(std::string(std::size_t(16) << 20, 'a'), true);
    std::atomic_bool holding{false}, stopped{false};
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        const auto channel = forward(server, 200, 0x7FFFFFFF);
        server.send(channelData(channel, script.request));
        std::this_thread::sleep_for(800ms);  // the answer fills the connection
        if (request) server.send(Writer().byte(GLOBAL_REQUEST).string("keepalive@openssh.com").boolean(true).take());
        if (viewer) openForwarded(server, 300);
        std::this_thread::sleep_for(300ms);
        holding = true;
        while (!stopped) std::this_thread::sleep_for(20ms);
    };
    FakeServer server(SSH_PORT, script);
    SshTunnel tunnel;
    tunnel.start(loopback(fingerprint(serverKey().blob)));
    REQUIRE(waitFor([&] { return holding.load() || server.done.load(); }, 30s));
    CHECK(holding.load());
    CHECK(tunnel.status().state == TunnelState::OnAir);
    tunnel.stop();
    stopped = true;
    CHECK(tunnelMemoryLeftBehind() == left_before);
}

}  // namespace

TEST_CASE("Tunnel: stopped with a packet half sent, the session is freed whole") { stopWhileHalfSent(false, false); }

TEST_CASE("Tunnel: stopped with a packet half sent and a viewer's connection not confirmed, the session is freed whole") {
    stopWhileHalfSent(false, true);
}

TEST_CASE("Tunnel: stopped with a packet half sent, a request not answered, a viewer's connection not confirmed") {
    stopWhileHalfSent(true, true);
}

TEST_CASE("Tunnel: what cannot be read ends the connection, though the session channel is closed") {
    // Waiting for viewers' connections, libssh2 tells any failure as a channel not found, which the tunnel took for
    // nothing serious: it stayed on air, deaf, until the server was found silent.
    std::atomic_bool corrupted{false};
    FakeServer::Script script;
    script.on_air = [&](FakeServer& server) {
        server.send(Writer().byte(CHANNEL_EOF).u32(server.session_channel).take());
        server.send(Writer().byte(CHANNEL_CLOSE).u32(server.session_channel).take());
        while (server.next()[0] != CHANNEL_CLOSE) {
        }
        server.sendRaw(randomBytes(64));
        corrupted = true;
    };
    FakeServer server(SSH_PORT, script);
    SshTunnel tunnel;
    tunnel.start(loopback(fingerprint(serverKey().blob)));
    REQUIRE(waitFor([&] { return corrupted.load() || server.done.load(); }));
    CHECK(waitFor([&] { return tunnel.status().state == TunnelState::Reconnecting; }, 5s));
    const auto error = tunnel.status().error;
    CHECK_FALSE(error.empty());
    CHECK_MESSAGE(error.find("not found") == std::string::npos, error);
}

TEST_CASE("Tunnel: server text in libssh2's traces is not taken for libssh2's own errors") {
    // The server's version line and its DEBUG messages are traced as they are: they told the tunnel of errors that
    // never happened (a host key's signature, failing it for good), or of a KEXINIT never sent.
    PacketCipher clear;
    FakeServer::Script script;
    script.strict  = false;
    script.version = "SSH-2.0-FakeServer_1.0 Failure Event: -11 - fake. Transport: Packet type 20 received";
    script.raw_first =
        clear.seal(Writer().byte(DEBUG_TEXT).boolean(true).string("Failure Event: -18 - fake too").string("").take(),
                   0) +
        std::string("\xFF\xFF\xFF\xFC", 4) + std::string(8, 'A');
    FakeServer server(SSH_PORT, script);
    SshTunnel tunnel;
    tunnel.start(loopback(fingerprint(serverKey().blob)));
    CHECK(waitFor([&] {
        const auto state = tunnel.status().state;
        return state == TunnelState::Reconnecting || state == TunnelState::Failed;
    }));
    const auto status = tunnel.status();
    CHECK(status.state == TunnelState::Reconnecting);
    CHECK_MESSAGE(status.error.find("fake") == std::string::npos, status.error);
    CHECK_MESSAGE(status.error.find("in common") == std::string::npos, status.error);
    CHECK(waitFor([&] { return server.done.load(); }));
}

TEST_CASE("Tunnel: a host key without a fingerprint is never trusted, nor kept") {
    const auto dir = std::filesystem::temp_directory_path() / "fcg-tunnel-no-fingerprint-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto known = dir / "known_hosts.json";
    // Pinned, or trusted on first use: refused, nothing kept, no key offered to trust.
    auto refusal = checkHostKey(loopback("SHA256:pinned"), "");
    REQUIRE(refusal);
    CHECK(refusal->reason.find("fingerprint") != std::string::npos);
    CHECK(refusal->new_key.empty());
    auto first        = loopback("");
    first.known_hosts = known;
    refusal           = checkHostKey(first, "");
    REQUIRE(refusal);
    CHECK(refusal->reason.find("fingerprint") != std::string::npos);
    CHECK(refusal->new_key.empty());
    CHECK(knownHostKey(known, "127.0.0.1", SSH_PORT) == "");
    // A fingerprint: trusted the first time and kept, the same one afterwards, another refused and offered.
    CHECK_FALSE(checkHostKey(first, "SHA256:aaa").has_value());
    CHECK(knownHostKey(known, "127.0.0.1", SSH_PORT) == "SHA256:aaa");
    CHECK_FALSE(checkHostKey(first, "SHA256:aaa").has_value());
    refusal = checkHostKey(first, "SHA256:bbb");
    REQUIRE(refusal);
    CHECK(refusal->permanent);
    CHECK(refusal->new_key == "SHA256:bbb");
    // And none again, a key kept: refused, the key kept as it was.
    CHECK(checkHostKey(first, "").has_value());
    CHECK(knownHostKey(known, "127.0.0.1", SSH_PORT) == "SHA256:aaa");
    std::filesystem::remove_all(dir);
}

TEST_CASE("Tunnel: the public address is forgotten once the connection is lost") {
    // The service may give the address to someone else meanwhile: it must not stay on screen to copy, open or scan.
    // Read though the shell request had no answer, as localhost.run gives none.
    std::atomic_bool release{false};
    FakeServer::Script script;
    script.answer_shell = false;
    script.on_air = [&](FakeServer& server) {
        while (!release) std::this_thread::sleep_for(20ms);
        server.send(Writer().byte(DISCONNECT).u32(11).string("going away").string("").take());
    };
    FakeServer server(SSH_PORT, script);
    SshTunnel tunnel;
    tunnel.start(loopback(fingerprint(serverKey().blob)));
    CHECK(waitFor([&] { return tunnel.status().url == "https://abc123.lhr.life"; }));
    release = true;
    CHECK(waitFor([&] { return tunnel.status().state == TunnelState::Reconnecting; }));
    CHECK(tunnel.status().url.empty());
    CHECK(tunnel.status().error.find("going away") != std::string::npos);
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
    // Port 1: nothing listens there, so nothing is served while the tunnel is up, 15 seconds or so. Keepalives
    // every 2 seconds: the server must answer them, or the tunnel would end after 11 silent seconds.
    auto settings      = localhostRun(1);
    settings.keepalive = 2s;
    SshTunnel tunnel;
    tunnel.start(settings);
    TunnelStatus status;
    waitFor(
        [&] {
            status = tunnel.status();
            return !status.url.empty() || status.state == TunnelState::Failed ||
                   status.state == TunnelState::Reconnecting;
        },
        30s);
    MESSAGE("state ", static_cast<int>(status.state), ", address ", status.url, ", error ", status.error);
    CHECK(status.state == TunnelState::OnAir);
    CHECK(status.url.rfind("https://", 0) == 0);
    std::this_thread::sleep_for(15s);
    status = tunnel.status();
    tunnel.stop();
    MESSAGE("15 s later: state ", static_cast<int>(status.state), ", error ", status.error);
    CHECK(status.state == TunnelState::OnAir);
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
