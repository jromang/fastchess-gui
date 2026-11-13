/* A libssh2 cryptography backend on the PSA API of Mbed TLS 4 (TF-PSA-Crypto 1.x), for fastchess-gui's tunnel.
 *
 * libssh2's own mbedtls backend needs the legacy RSA, ECP, cipher and bignum APIs, which Mbed TLS 4 made private.
 * This one uses PSA only, and offers what a client needs to reach a server with an RSA host key:
 *
 *   key exchange   curve25519-sha256 (X25519 through PSA ECDH on Montgomery 255)
 *   host keys      rsa-sha2-256, rsa-sha2-512 (verification only, 2048 to 4096 bits)
 *   ciphers        aes128-ctr, aes192-ctr, aes256-ctr, aes128-gcm@openssh.com, aes256-gcm@openssh.com
 *   MACs           hmac-sha1, hmac-sha2-256, hmac-sha2-512 (and their -etm@openssh.com forms)
 *
 * libssh2 compiles curve25519 only along with Ed25519 (LIBSSH2_ED25519), which PSA does not have: the Ed25519
 * functions are there and fail, so an ssh-ed25519 host key is refused if it is ever negotiated. Nothing private-key
 * (public key authentication, key files) is supported, nor the classic Diffie-Hellman groups, whose bignums PSA does
 * not offer: their functions fail too. The caller restricts the negotiation to the algorithms above with
 * libssh2_session_method_pref(), so none of these is reached.
 *
 * libssh2_config.h includes this file (see there); mbedtls_psa.c implements it. */

#ifndef FCG_LIBSSH2_MBEDTLS_PSA_H
#define FCG_LIBSSH2_MBEDTLS_PSA_H

#include <stddef.h>
#include <stdint.h>

#include <psa/crypto.h>

#define SSH2_CRYPTO_ENGINE      libssh2_mbedtls
#define SSH2_CRYPTO_ENGINE_NAME "mbedTLS-PSA"

/* What is supported. */
#define LIBSSH2_MD5          0
#define LIBSSH2_HMAC_RIPEMD  0
#define LIBSSH2_HMAC_SHA256  1
#define LIBSSH2_HMAC_SHA512  1

#define LIBSSH2_AES_CBC      0
#define LIBSSH2_AES_CTR      1
#define LIBSSH2_AES_GCM      1
#define LIBSSH2_BLOWFISH     0
#define LIBSSH2_RC4          0
#define LIBSSH2_CAST         0
#define LIBSSH2_3DES         0

#define LIBSSH2_RSA          1
#define LIBSSH2_RSA_SHA1     0
#define LIBSSH2_RSA_SHA2     1
#define LIBSSH2_DSA          0
#define LIBSSH2_ECDSA        0
#define LIBSSH2_ED25519      1
#define LIBSSH2_MLKEM        0

#include "crypto_config.h"

/* Hashes */

#define ssh2_hash_ctx          psa_hash_operation_t
#define ssh2_hash_alg          psa_algorithm_t
#define ssh2_hash_update(ctx, data, len) \
    (psa_hash_update(ctx, (const uint8_t *)(data), len) == PSA_SUCCESS)

#define SSH2_SHA1_ALG          PSA_ALG_SHA_1
#define SSH2_SHA256_ALG        PSA_ALG_SHA_256
#define SSH2_SHA384_ALG        PSA_ALG_SHA_384
#define SSH2_SHA512_ALG        PSA_ALG_SHA_512

/* HMAC: the key imported for the operation, destroyed with it */

struct ssh2_psa_hmac_ctx {
    psa_mac_operation_t mac;
    psa_key_id_t key;
};

#define ssh2_hmac_ctx          struct ssh2_psa_hmac_ctx
#define ssh2_hmac_update(ctx, data, len) \
    (psa_mac_update(&(ctx)->mac, (const uint8_t *)(data), len) == PSA_SUCCESS)

/* RSA: a public key, imported in PSA, and its size */

struct ssh2_psa_rsa_ctx {
    psa_key_id_t key;
    size_t bytes; /* of the modulus, which a signature is as long as */
};

#define ssh2_rsa_ctx           struct ssh2_psa_rsa_ctx

/* Ed25519: not supported, every function fails */

struct ssh2_psa_ed25519_ctx {
    int unused;
};

#define ssh2_ed25519_ctx       struct ssh2_psa_ed25519_ctx

/* Ciphers: AES-CTR through a PSA cipher operation that lasts as long as the keys, AES-GCM through a PSA AEAD
 * operation for each packet, with the nonce of RFC 5647 (a fixed part and a packet counter). */

struct ssh2_psa_cipher_ctx {
    int algo;
    int encrypt;
    psa_key_id_t key;
    psa_cipher_operation_t ctr;
    psa_aead_operation_t gcm;
    unsigned char nonce[12];
};

#define ssh2_cipher_ctx        struct ssh2_psa_cipher_ctx
#define SSH2_CIPHER_T(algo)    int algo

#define ssh2_cipher_aes128ctr  1
#define ssh2_cipher_aes192ctr  2
#define ssh2_cipher_aes256ctr  3
#define ssh2_cipher_aes128gcm  4
#define ssh2_cipher_aes256gcm  5
/* libssh2's own ChaCha20-Poly1305 (chacha.c, poly1305.c) does not come here: the value is not used. */
#define ssh2_cipher_chacha20   6

/* Big numbers: only carried (the shared secret of curve25519, as libssh2 encodes it into an mpint), never
 * computed with: big-endian bytes, without leading zeros. */

struct ssh2_psa_bn {
    unsigned char *data;
    size_t len;
};

#define ssh2_bn                struct ssh2_psa_bn
size_t ssh2_bn_bytes(const struct ssh2_psa_bn *bn);

/* Diffie-Hellman: not supported (see above), but its sizes are part of what libssh2 compiles. */

#define ssh2_dh_ctx            struct ssh2_psa_bn *

#define SSH2_DH_GEX_MINGROUP     2048
#define SSH2_DH_GEX_OPTGROUP     4096
#define SSH2_DH_GEX_MAXGROUP     8192
#define SSH2_DH_MAX_MODULUS_BITS 16384

/* libssh2_exit() must not end PSA: the rest of the program uses it (TLS), before and after. */
#define ssh2_crypto_exit()     do {} while(0)

#endif /* FCG_LIBSSH2_MBEDTLS_PSA_H */
