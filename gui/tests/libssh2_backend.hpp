#pragma once

// The functions of our libssh2 cryptography backend (gui/libssh2-psa) the tests call directly, declared as libssh2's
// crypto.h declares them: that header needs all of libssh2's private ones.

#include <libssh2.h>

extern "C" {

#include "mbedtls_psa.h"

int ssh2_hash_init(ssh2_hash_ctx* ctx, ssh2_hash_alg alg);
int ssh2_hash_final(ssh2_hash_ctx* ctx, void* digest, size_t digest_len);

int ssh2_hmac_ctx_init(ssh2_hmac_ctx* ctx);
int ssh2_hmac_init(ssh2_hmac_ctx* ctx, ssh2_hash_alg alg, void* key, size_t key_len);
int ssh2_hmac_final(ssh2_hmac_ctx* ctx, void* mac, size_t mac_len);
void ssh2_hmac_cleanup(ssh2_hmac_ctx* ctx);

int ssh2_rsa_new(ssh2_rsa_ctx** rsa, LIBSSH2_SESSION* session, const unsigned char* edata, size_t elen,
                 const unsigned char* ndata, size_t nlen, const unsigned char* ddata, size_t dlen,
                 const unsigned char* pdata, size_t plen, const unsigned char* qdata, size_t qlen,
                 const unsigned char* e1data, size_t e1len, const unsigned char* e2data, size_t e2len,
                 const unsigned char* coeffdata, size_t coefflen);
int ssh2_rsa_sha2_verify(ssh2_rsa_ctx* rsa, LIBSSH2_SESSION* session, size_t hash_len, const unsigned char* sig,
                         size_t sig_len, const unsigned char* m, size_t m_len);
void ssh2_rsa_free(ssh2_rsa_ctx* rsa, LIBSSH2_SESSION* session);

int ssh2_curve25519_new(LIBSSH2_SESSION* session, uint8_t** out_public_key, uint8_t** out_private_key);
int ssh2_curve25519_gen_k(ssh2_bn** k, uint8_t private_key[32], uint8_t server_public_key[32]);
int ssh2_ed25519_verify(ssh2_ed25519_ctx* ed_ctx, LIBSSH2_SESSION* session, const uint8_t* s, size_t s_len,
                        const uint8_t* m, size_t m_len);

ssh2_bn* ssh2_bn_init(void);
void ssh2_bn_free(ssh2_bn* bn);
size_t ssh2_bn_bits(const ssh2_bn* bn);
int ssh2_bn_to_bin(const ssh2_bn* bn, unsigned char* bin);

int ssh2_cipher_init(ssh2_cipher_ctx* ctx, int algo, unsigned char* iv, unsigned char* secret, int encrypt);
int ssh2_cipher_crypt(ssh2_cipher_ctx* ctx, int algo, int encrypt, unsigned char* block, size_t blocksize,
                      int firstlast);
void ssh2_cipher_dtor(ssh2_cipher_ctx* ctx);

int ssh2_dh_key_pair(ssh2_dh_ctx* dhctx, ssh2_bn* pub, const ssh2_bn* g, const ssh2_bn* p, int group_order,
                     int* bnctx);
}

// libssh2_priv.h's, for ssh2_cipher_crypt().
constexpr int SSH2_FIRST_BLOCK  = 1;
constexpr int SSH2_MIDDLE_BLOCK = 0;
constexpr int SSH2_LAST_BLOCK   = 2;
