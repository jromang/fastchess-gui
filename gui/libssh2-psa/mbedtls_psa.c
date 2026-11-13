/* A libssh2 cryptography backend on the PSA API of Mbed TLS 4: see mbedtls_psa.h.
 *
 * The hash and HMAC functions follow libssh2's mbedtls.c (Copyright (C) Art <https://github.com/wildart>), which
 * already used PSA for them.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "libssh2_priv.h"

#include <stdlib.h>

#include <mbedtls/platform_util.h>

/* Every buffer handed to PSA here is at most this long: one block of libssh2's packet layer with its tag (a GCM
 * packet's last block is up to two blocks), or an RSA signature or key of PSA's largest size. */
#define PSA_BUF_MAX 64

void ssh2_crypto_init(void)
{
    (void)psa_crypto_init();
}

int ssh2_random(unsigned char *buf, size_t len)
{
    return psa_generate_random(buf, len) == PSA_SUCCESS ? 0 : -1;
}

/* A key imported for one use. 0 when it cannot be. */
static psa_key_id_t psa_import(psa_key_type_t type, size_t bits, psa_algorithm_t alg, psa_key_usage_t usage,
                               const unsigned char *data, size_t len)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t key = PSA_KEY_ID_NULL;

    psa_set_key_type(&attributes, type);
    if(bits)
        psa_set_key_bits(&attributes, bits);
    psa_set_key_algorithm(&attributes, alg);
    psa_set_key_usage_flags(&attributes, usage);
    if(psa_import_key(&attributes, data, len, &key) != PSA_SUCCESS)
        key = PSA_KEY_ID_NULL;
    psa_reset_key_attributes(&attributes);
    return key;
}

/*******************************************************************/
/* Hashes and HMAC */

int ssh2_hash_init(ssh2_hash_ctx *ctx, ssh2_hash_alg alg)
{
    *ctx = psa_hash_operation_init();
    return psa_hash_setup(ctx, alg) == PSA_SUCCESS;
}

int ssh2_hash_final(ssh2_hash_ctx *ctx, void *digest, size_t digest_len)
{
    /* digest_len is the buffer's size: libssh2 gives the largest digest's room at times. */
    size_t actual_len = 0;
    int ok = psa_hash_finish(ctx, digest, digest_len, &actual_len) == PSA_SUCCESS;

    if(!ok)
        (void)psa_hash_abort(ctx);
    return ok;
}

int ssh2_hmac_ctx_init(ssh2_hmac_ctx *ctx)
{
    ctx->mac = psa_mac_operation_init();
    ctx->key = PSA_KEY_ID_NULL;
    return 1;
}

int ssh2_hmac_init(ssh2_hmac_ctx *ctx, ssh2_hmac_alg alg, void *key, size_t key_len)
{
    psa_algorithm_t hmac = PSA_ALG_HMAC(alg);

    ctx->key = psa_import(PSA_KEY_TYPE_HMAC, 0, hmac, PSA_KEY_USAGE_SIGN_MESSAGE, key, key_len);
    if(ctx->key == PSA_KEY_ID_NULL)
        return 0;
    if(psa_mac_sign_setup(&ctx->mac, ctx->key, hmac) != PSA_SUCCESS) {
        ssh2_hmac_cleanup(ctx);
        return 0;
    }
    return 1;
}

int ssh2_hmac_final(ssh2_hmac_ctx *ctx, void *mac, size_t mac_len)
{
    size_t actual_len = 0;
    return psa_mac_sign_finish(&ctx->mac, mac, mac_len, &actual_len) == PSA_SUCCESS;
}

void ssh2_hmac_cleanup(ssh2_hmac_ctx *ctx)
{
    (void)psa_mac_abort(&ctx->mac);
    (void)psa_destroy_key(ctx->key);
    ctx->key = PSA_KEY_ID_NULL;
}

/*******************************************************************/
/* Ciphers */

static int cipher_is_gcm(int algo)
{
    return algo == ssh2_cipher_aes128gcm || algo == ssh2_cipher_aes256gcm;
}

static size_t cipher_key_len(int algo)
{
    switch(algo) {
    case ssh2_cipher_aes128ctr:
    case ssh2_cipher_aes128gcm:
        return 16;
    case ssh2_cipher_aes192ctr:
        return 24;
    case ssh2_cipher_aes256ctr:
    case ssh2_cipher_aes256gcm:
        return 32;
    default:
        return 0;
    }
}

int ssh2_cipher_init(ssh2_cipher_ctx *ctx, SSH2_CIPHER_T(algo), unsigned char *iv, unsigned char *secret,
                     int encrypt)
{
    size_t key_len = cipher_key_len(algo);
    int gcm = cipher_is_gcm(algo);

    ctx->algo = algo;
    ctx->encrypt = encrypt;
    ctx->ctr = psa_cipher_operation_init();
    ctx->gcm = psa_aead_operation_init();
    memset(ctx->nonce, 0, sizeof(ctx->nonce));
    ctx->key = PSA_KEY_ID_NULL;
    if(!key_len)
        return 1;

    ctx->key = psa_import(PSA_KEY_TYPE_AES, key_len * 8, gcm ? PSA_ALG_GCM : PSA_ALG_CTR,
                          PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT, secret, key_len);
    if(ctx->key == PSA_KEY_ID_NULL)
        return 1;

    if(gcm) {
        /* The fixed part and the invocation counter (RFC 5647, 7.1), 12 bytes. */
        memcpy(ctx->nonce, iv, sizeof(ctx->nonce));
        return 0;
    }
    /* CTR is its own inverse: an encryption serves both directions, its counter kept from packet to packet. */
    if(psa_cipher_encrypt_setup(&ctx->ctr, ctx->key, PSA_ALG_CTR) != PSA_SUCCESS ||
       psa_cipher_set_iv(&ctx->ctr, iv, 16) != PSA_SUCCESS) {
        ssh2_cipher_dtor(ctx);
        return 1;
    }
    return 0;
}

/* The next packet's nonce: the invocation counter, its last 8 bytes, plus one. */
static void gcm_next_nonce(unsigned char nonce[12])
{
    int i;
    for(i = 11; i >= 4; --i) {
        if(++nonce[i])
            break;
    }
}

/* One part of a packet (libssh2's transport.c): the first one begins with the packet length, authenticated but
 * not encrypted, and the last one ends with the tag, written or checked. */
static int gcm_crypt(ssh2_cipher_ctx *ctx, unsigned char *block, size_t blocksize, int firstlast)
{
    const size_t aad_len = IS_FIRST(firstlast) ? 4 : 0;
    const size_t tag_len = IS_LAST(firstlast) ? 16 : 0;
    unsigned char out[PSA_BUF_MAX];
    size_t crypt_len, n = 0;
    psa_status_t status = PSA_SUCCESS;

    if(blocksize < aad_len + tag_len)
        return 1;
    crypt_len = blocksize - aad_len - tag_len;
    /* GCM may give its output a block later (mbedtls_gcm_update()): room for that, though it is an error here,
       where every part is decrypted at once. */
    if(crypt_len + 16 > sizeof(out))
        return 1;

    if(IS_FIRST(firstlast)) {
        (void)psa_aead_abort(&ctx->gcm);
        ctx->gcm = psa_aead_operation_init();
        status = ctx->encrypt ? psa_aead_encrypt_setup(&ctx->gcm, ctx->key, PSA_ALG_GCM)
                              : psa_aead_decrypt_setup(&ctx->gcm, ctx->key, PSA_ALG_GCM);
        if(status == PSA_SUCCESS)
            status = psa_aead_set_nonce(&ctx->gcm, ctx->nonce, sizeof(ctx->nonce));
        gcm_next_nonce(ctx->nonce);
        if(status == PSA_SUCCESS)
            status = psa_aead_update_ad(&ctx->gcm, block, aad_len);
    }
    if(status == PSA_SUCCESS && crypt_len) {
        status = psa_aead_update(&ctx->gcm, block + aad_len, crypt_len, out, sizeof(out), &n);
        if(status == PSA_SUCCESS && n != crypt_len)
            status = PSA_ERROR_GENERIC_ERROR;
        if(status == PSA_SUCCESS)
            memcpy(block + aad_len, out, crypt_len);
    }
    if(status == PSA_SUCCESS && IS_LAST(firstlast)) {
        unsigned char *tag = block + blocksize - tag_len;
        size_t written = 0;
        if(ctx->encrypt) {
            status = psa_aead_finish(&ctx->gcm, out, sizeof(out), &n, tag, tag_len, &written);
            if(status == PSA_SUCCESS && (n != 0 || written != tag_len))
                status = PSA_ERROR_GENERIC_ERROR;
        }
        else {
            status = psa_aead_verify(&ctx->gcm, out, sizeof(out), &n, tag, tag_len);
            if(status == PSA_SUCCESS && n != 0)
                status = PSA_ERROR_GENERIC_ERROR;
        }
    }
    mbedtls_platform_zeroize(out, sizeof(out));
    if(status != PSA_SUCCESS) {
        (void)psa_aead_abort(&ctx->gcm);
        return 1;
    }
    return 0;
}

int ssh2_cipher_crypt(ssh2_cipher_ctx *ctx, SSH2_CIPHER_T(algo), int encrypt, unsigned char *block,
                      size_t blocksize, int firstlast)
{
    unsigned char out[PSA_BUF_MAX];
    int bad = 0;

    (void)encrypt;
    if(algo != ctx->algo || ctx->key == PSA_KEY_ID_NULL)
        return 1;
    if(cipher_is_gcm(algo))
        return gcm_crypt(ctx, block, blocksize, firstlast);

    /* libssh2 gives a block at a time; any length is taken, in parts. */
    while(blocksize && !bad) {
        size_t part = blocksize < sizeof(out) - 16 ? blocksize : sizeof(out) - 16;
        size_t n = 0;
        bad = psa_cipher_update(&ctx->ctr, block, part, out, sizeof(out), &n) != PSA_SUCCESS || n != part;
        if(!bad)
            memcpy(block, out, part);
        block += part;
        blocksize -= part;
    }
    mbedtls_platform_zeroize(out, sizeof(out));
    return bad;
}

void ssh2_cipher_dtor(ssh2_cipher_ctx *ctx)
{
    (void)psa_cipher_abort(&ctx->ctr);
    (void)psa_aead_abort(&ctx->gcm);
    (void)psa_destroy_key(ctx->key);
    ctx->key = PSA_KEY_ID_NULL;
    mbedtls_platform_zeroize(ctx->nonce, sizeof(ctx->nonce));
}

/*******************************************************************/
/* Big numbers, carried only */

ssh2_bn *ssh2_bn_init(void)
{
    return calloc(1, sizeof(ssh2_bn));
}

void ssh2_bn_free(ssh2_bn *bn)
{
    if(!bn)
        return;
    if(bn->data) {
        mbedtls_platform_zeroize(bn->data, bn->len);
        free(bn->data);
    }
    free(bn);
}

int ssh2_bn_from_bin(ssh2_bn **bn, const unsigned char *bin, size_t len)
{
    unsigned char *data = NULL;

    if(!bn || (!bin && len))
        return -1;
    if(!*bn) {
        *bn = ssh2_bn_init();
        if(!*bn)
            return -1;
    }
    while(len && !*bin) {
        ++bin;
        --len;
    }
    if(len) {
        data = malloc(len);
        if(!data)
            return -1;
        memcpy(data, bin, len);
    }
    if((*bn)->data) {
        mbedtls_platform_zeroize((*bn)->data, (*bn)->len);
        free((*bn)->data);
    }
    (*bn)->data = data;
    (*bn)->len = len;
    return 0;
}

int ssh2_bn_set_word(ssh2_bn *bn, uint32_t word)
{
    unsigned char bin[4];
    ssh2_htonu32(bin, word);
    return ssh2_bn_from_bin(&bn, bin, sizeof(bin));
}

size_t ssh2_bn_bytes(const ssh2_bn *bn)
{
    return bn->len;
}

size_t ssh2_bn_bits(const ssh2_bn *bn)
{
    size_t bits = bn->len * 8;
    unsigned char top = bn->len ? bn->data[0] : 0;

    if(!bn->len)
        return 0;
    while(!(top & 0x80)) {
        top = (unsigned char)(top << 1);
        --bits;
    }
    return bits;
}

int ssh2_bn_to_bin(const ssh2_bn *bn, unsigned char *bin)
{
    if(bn->len)
        memcpy(bin, bn->data, bn->len);
    return 0;
}

/*******************************************************************/
/* Diffie-Hellman groups: not supported */

void ssh2_dh_init(ssh2_dh_ctx *dhctx)
{
    *dhctx = NULL;
}

int ssh2_dh_key_pair(ssh2_dh_ctx *dhctx, ssh2_bn *pub, const ssh2_bn *g, const ssh2_bn *p, int group_order,
                     ssh2_bn_ctx *bnctx)
{
    (void)dhctx;
    (void)pub;
    (void)g;
    (void)p;
    (void)group_order;
    (void)bnctx;
    return -1;
}

int ssh2_dh_validate(const ssh2_bn *f, const ssh2_bn *p)
{
    (void)f;
    (void)p;
    return -1;
}

int ssh2_dh_secret(ssh2_dh_ctx *dhctx, ssh2_bn *secret, const ssh2_bn *f, const ssh2_bn *p, ssh2_bn_ctx *bnctx)
{
    (void)dhctx;
    (void)secret;
    (void)f;
    (void)p;
    (void)bnctx;
    return -1;
}

void ssh2_dh_dtor(ssh2_dh_ctx *dhctx)
{
    ssh2_bn_free(*dhctx);
    *dhctx = NULL;
}

/*******************************************************************/
/* RSA: host keys, verified */

/* A DER length (X.690, 8.1.3). */
static unsigned char *der_length(unsigned char *out, size_t len)
{
    if(len < 0x80)
        *out++ = (unsigned char)len;
    else if(len < 0x100) {
        *out++ = 0x81;
        *out++ = (unsigned char)len;
    }
    else {
        *out++ = 0x82;
        *out++ = (unsigned char)(len >> 8);
        *out++ = (unsigned char)len;
    }
    return out;
}

/* A positive INTEGER of `len` big-endian bytes without leading zeros: its size, or its encoding when `out` is
   given. */
static size_t der_integer(unsigned char *out, const unsigned char *data, size_t len)
{
    size_t value_len = len + ((data[0] & 0x80) ? 1 : 0);
    unsigned char head[4];
    size_t head_len = (size_t)(der_length(head, value_len) - head) + 1;

    if(out) {
        *out++ = 0x02;
        memcpy(out, head, head_len - 1);
        out += head_len - 1;
        if(value_len > len)
            *out++ = 0;
        memcpy(out, data, len);
    }
    return head_len + value_len;
}

static const unsigned char *skip_zeros(const unsigned char *data, size_t *len)
{
    while(*len && !*data) {
        ++data;
        --*len;
    }
    return data;
}

int ssh2_rsa_new(ssh2_rsa_ctx **rsa, LIBSSH2_SESSION *session,
                 const unsigned char *edata, size_t elen,
                 const unsigned char *ndata, size_t nlen,
                 const unsigned char *ddata, size_t dlen,
                 const unsigned char *pdata, size_t plen,
                 const unsigned char *qdata, size_t qlen,
                 const unsigned char *e1data, size_t e1len,
                 const unsigned char *e2data, size_t e2len,
                 const unsigned char *coeffdata, size_t coefflen)
{
    /* RSAPublicKey ::= SEQUENCE { modulus INTEGER, publicExponent INTEGER } (RFC 8017, A.1.1) */
    unsigned char *der, *p;
    size_t body_len, der_len, bits;
    ssh2_rsa_ctx *ctx;

    (void)dlen;
    (void)pdata;
    (void)plen;
    (void)qdata;
    (void)qlen;
    (void)e1data;
    (void)e1len;
    (void)e2data;
    (void)e2len;
    (void)coeffdata;
    (void)coefflen;

    *rsa = NULL;
    /* Public keys only. */
    if(ddata)
        return -1;
    /* Positive numbers (an mpint with its high bit set is negative). */
    if(!elen || !nlen || (edata[0] & 0x80) || (ndata[0] & 0x80))
        return -1;
    edata = skip_zeros(edata, &elen);
    ndata = skip_zeros(ndata, &nlen);
    if(!elen || !nlen || elen > 8)
        return -1;
    /* In bits, not bytes: a 2041-bit modulus takes 256 bytes too. */
    bits = nlen * 8;
    {
        unsigned char top = ndata[0];
        while(!(top & 0x80)) {
            top = (unsigned char)(top << 1);
            --bits;
        }
    }
    if(bits < 2048 || bits > PSA_VENDOR_RSA_MAX_KEY_BITS)
        return -1;

    body_len = der_integer(NULL, ndata, nlen) + der_integer(NULL, edata, elen);
    der_len = 4 + body_len;
    der = SSH2_ALLOC(session, der_len);
    if(!der)
        return -1;
    der[0] = 0x30;
    p = der_length(der + 1, body_len);
    p += der_integer(p, ndata, nlen);
    p += der_integer(p, edata, elen);
    der_len = (size_t)(p - der);

    ctx = SSH2_ALLOC(session, sizeof(*ctx));
    if(!ctx) {
        SSH2_FREE(session, der);
        return -1;
    }
    ctx->bytes = nlen;
    ctx->key = psa_import(PSA_KEY_TYPE_RSA_PUBLIC_KEY, 0, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_ANY_HASH),
                          PSA_KEY_USAGE_VERIFY_MESSAGE, der, der_len);
    SSH2_FREE(session, der);
    if(ctx->key == PSA_KEY_ID_NULL) {
        SSH2_FREE(session, ctx);
        return -1;
    }
    *rsa = ctx;
    return 0;
}

int ssh2_rsa_new_priv(ssh2_rsa_ctx **rsa, LIBSSH2_SESSION *session, const char *filename, const char *blob,
                      size_t blob_len, const char *passphrase)
{
    (void)filename;
    (void)blob;
    (void)blob_len;
    (void)passphrase;
    *rsa = NULL;
    return ssh2_err(session, LIBSSH2_ERROR_FILE, "Private keys are not supported by this backend");
}

int ssh2_rsa_sha2_verify(ssh2_rsa_ctx *rsa, LIBSSH2_SESSION *session, size_t hash_len, const unsigned char *sig,
                         size_t sig_len, const unsigned char *m, size_t m_len)
{
    psa_algorithm_t alg;
    unsigned char *padded;
    psa_status_t status;

    if(hash_len == SSH2_SHA256_DIG_LEN)
        alg = PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256);
    else if(hash_len == SSH2_SHA512_DIG_LEN)
        alg = PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_512);
    else
        return -1;
    /* As long as the modulus: some servers leave out the signature's leading zeros. */
    if(!sig_len || sig_len > rsa->bytes)
        return -1;
    if(sig_len == rsa->bytes)
        return psa_verify_message(rsa->key, alg, m, m_len, sig, sig_len) == PSA_SUCCESS ? 0 : -1;

    padded = SSH2_ALLOC(session, rsa->bytes);
    if(!padded)
        return -1;
    memset(padded, 0, rsa->bytes - sig_len);
    memcpy(padded + rsa->bytes - sig_len, sig, sig_len);
    status = psa_verify_message(rsa->key, alg, m, m_len, padded, rsa->bytes);
    SSH2_FREE(session, padded);
    return status == PSA_SUCCESS ? 0 : -1;
}

int ssh2_rsa_sha2_sign(ssh2_rsa_ctx *rsa, LIBSSH2_SESSION *session, const unsigned char *hash, size_t hash_len,
                       unsigned char **signature, size_t *signature_len)
{
    (void)rsa;
    (void)session;
    (void)hash;
    (void)hash_len;
    *signature = NULL;
    *signature_len = 0;
    return -1;
}

void ssh2_rsa_free(ssh2_rsa_ctx *rsa, LIBSSH2_SESSION *session)
{
    if(!rsa)
        return;
    (void)psa_destroy_key(rsa->key);
    SSH2_FREE(session, rsa);
}

/*******************************************************************/
/* Curve25519 key exchange (RFC 8731), through PSA ECDH */

static psa_key_id_t x25519_import(const uint8_t private_key[SSH2_ED25519_KEY_LEN])
{
    return psa_import(PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY), 255, PSA_ALG_ECDH,
                      PSA_KEY_USAGE_DERIVE, private_key, SSH2_ED25519_KEY_LEN);
}

int ssh2_curve25519_new(LIBSSH2_SESSION *session, uint8_t **out_public_key, uint8_t **out_private_key)
{
    uint8_t *priv, *pub;
    psa_key_id_t key;
    size_t n = 0;
    int ok;

    priv = SSH2_ALLOC(session, SSH2_ED25519_KEY_LEN);
    pub = SSH2_ALLOC(session, SSH2_ED25519_KEY_LEN);
    if(!priv || !pub) {
        if(priv)
            SSH2_FREE(session, priv);
        if(pub)
            SSH2_FREE(session, pub);
        return -1;
    }
    /* 32 random bytes are an X25519 private key (RFC 7748, 5: the scalar is clamped when used). */
    ok = psa_generate_random(priv, SSH2_ED25519_KEY_LEN) == PSA_SUCCESS;
    key = ok ? x25519_import(priv) : PSA_KEY_ID_NULL;
    ok = key != PSA_KEY_ID_NULL &&
         psa_export_public_key(key, pub, SSH2_ED25519_KEY_LEN, &n) == PSA_SUCCESS && n == SSH2_ED25519_KEY_LEN;
    (void)psa_destroy_key(key);
    if(!ok) {
        ssh2_zero_free(session, priv, SSH2_ED25519_KEY_LEN);
        SSH2_FREE(session, pub);
        return -1;
    }
    if(out_private_key)
        *out_private_key = priv;
    else
        ssh2_zero_free(session, priv, SSH2_ED25519_KEY_LEN);
    if(out_public_key)
        *out_public_key = pub;
    else
        SSH2_FREE(session, pub);
    return 0;
}

int ssh2_curve25519_gen_k(ssh2_bn **k, uint8_t private_key[SSH2_ED25519_KEY_LEN],
                          uint8_t server_public_key[SSH2_ED25519_KEY_LEN])
{
    uint8_t secret[SSH2_ED25519_KEY_LEN];
    uint8_t any = 0;
    psa_key_id_t key;
    size_t i, n = 0;
    int rc = -1;

    if(!k || !*k)
        return -1;
    key = x25519_import(private_key);
    if(key == PSA_KEY_ID_NULL)
        return -1;
    if(psa_raw_key_agreement(PSA_ALG_ECDH, key, server_public_key, SSH2_ED25519_KEY_LEN, secret, sizeof(secret),
                             &n) == PSA_SUCCESS &&
       n == sizeof(secret)) {
        /* A server key of small order makes the secret zero: refused (RFC 7748, 6.1; RFC 8731, 3). */
        for(i = 0; i < sizeof(secret); ++i)
            any |= secret[i];
        /* The secret as an unsigned big-endian number (RFC 8731, 3.1). */
        if(any)
            rc = ssh2_bn_from_bin(k, secret, sizeof(secret));
    }
    (void)psa_destroy_key(key);
    mbedtls_platform_zeroize(secret, sizeof(secret));
    return rc;
}

/*******************************************************************/
/* Ed25519: not in PSA, every function fails */

int ssh2_ed25519_new_public(ssh2_ed25519_ctx **ed_ctx, LIBSSH2_SESSION *session, const unsigned char *raw_pub_key,
                            const size_t key_len)
{
    (void)raw_pub_key;
    (void)key_len;
    *ed_ctx = NULL;
    return ssh2_err(session, LIBSSH2_ERROR_METHOD_NOT_SUPPORTED, "Ed25519 is not supported by this backend");
}

int ssh2_ed25519_new_priv(ssh2_ed25519_ctx **ed_ctx, LIBSSH2_SESSION *session, const char *filename,
                          const char *blob, size_t blob_len, const char *passphrase)
{
    (void)filename;
    (void)blob;
    (void)blob_len;
    (void)passphrase;
    *ed_ctx = NULL;
    return ssh2_err(session, LIBSSH2_ERROR_METHOD_NOT_SUPPORTED, "Ed25519 is not supported by this backend");
}

int ssh2_ed25519_sign(ssh2_ed25519_ctx *ed_ctx, LIBSSH2_SESSION *session, uint8_t **out_sig, size_t *out_sig_len,
                      const uint8_t *message, size_t message_len)
{
    (void)ed_ctx;
    (void)session;
    (void)message;
    (void)message_len;
    *out_sig = NULL;
    *out_sig_len = 0;
    return -1;
}

int ssh2_ed25519_verify(ssh2_ed25519_ctx *ed_ctx, LIBSSH2_SESSION *session, const uint8_t *s, size_t s_len,
                        const uint8_t *m, size_t m_len)
{
    (void)ed_ctx;
    (void)session;
    (void)s;
    (void)s_len;
    (void)m;
    (void)m_len;
    return -1;
}

void ssh2_ed25519_free(ssh2_ed25519_ctx *ed_ctx, LIBSSH2_SESSION *session)
{
    if(ed_ctx)
        SSH2_FREE(session, ed_ctx);
}

/*******************************************************************/
/* Private keys: not supported (security keys neither: misc.c's ssh2_sk_pubkey() refuses them without
   LIBSSH2_KEY_SK) */

int ssh2_pub_privkey(LIBSSH2_SESSION *session, char **method, unsigned char **pubkeydata, size_t *pubkeydata_len,
                     const char *privkeyfile, const char *privkeyblob, size_t privkeyblob_len,
                     const char *passphrase)
{
    (void)privkeyfile;
    (void)privkeyblob;
    (void)privkeyblob_len;
    (void)passphrase;
    *method = NULL;
    *pubkeydata = NULL;
    *pubkeydata_len = 0;
    return ssh2_err(session, LIBSSH2_ERROR_FILE, "Private keys are not supported by this backend");
}
