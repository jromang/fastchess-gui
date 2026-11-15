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

#include "ed25519.h"

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

    /* Into the number given: ssh2_bn_from_bin() would make one of its own for a NULL, never seen by the caller. */
    if(!bn)
        return -1;
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
/* RSA: host keys, verified; the user's keys, which sign (below, with the private key files) */

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

/*******************************************************************/
/* Private keys: the user's, for public key authentication on the remote machines */

/* A private key file read: RSA's numbers (big-endian, without leading zeros) or Ed25519's seed and public key, which
   point into `decrypted` (the OpenSSH format) or `der` (PKCS #1), freed by priv_key_free(). For the PEM format,
   `der` is the RSAPrivateKey PSA imports as it is. */
struct priv_key {
    int type; /* PRIV_RSA or PRIV_ED25519 */
    struct string_buf *decrypted;
    unsigned char *der;
    size_t der_len;
    const unsigned char *n, *e, *d, *p, *q, *qinv;
    size_t n_len, e_len, d_len, p_len, q_len, qinv_len;
    const unsigned char *ed_seed, *ed_pub;
};

#define PRIV_RSA     1
#define PRIV_ED25519 2

#define OPENSSH_HEADER "-----BEGIN OPENSSH PRIVATE KEY-----"

static void priv_key_free(LIBSSH2_SESSION *session, struct priv_key *key)
{
    if(key->decrypted) {
        if(key->decrypted->data)
            mbedtls_platform_zeroize(key->decrypted->data, key->decrypted->len);
        ssh2_string_buf_free(session, key->decrypted);
    }
    if(key->der)
        ssh2_zero_free(session, key->der, key->der_len);
    memset(key, 0, sizeof(*key));
}

static int contains(const char *data, size_t len, const char *text)
{
    size_t n = strlen(text), i;
    for(i = 0; i + n <= len; ++i) {
        if(!memcmp(data + i, text, n))
            return 1;
    }
    return 0;
}

/* The next INTEGER of a DER sequence, without its leading zeros. */
static int der_next(unsigned char **data, size_t *len, const unsigned char **value, size_t *value_len)
{
    unsigned char *i;
    unsigned int ilen;
    size_t n;
    if(ssh2_pem_decode_integer(data, len, &i, &ilen))
        return -1;
    n = ilen;
    *value = skip_zeros(i, &n);
    *value_len = n;
    return 0;
}

/* The key of `filename`, or of `blob` when there is no file name. libssh2's error is set when it fails. */
static int priv_key_read(LIBSSH2_SESSION *session, const char *filename, const char *blob, size_t blob_len,
                         const char *passphrase, struct priv_key *key)
{
    char *filedata = NULL;
    size_t filedata_len = 0;
    int rc = -1;

    memset(key, 0, sizeof(*key));
    if(filename) {
        rc = ssh2_file_to_blob(session, filename, &filedata, &filedata_len);
        if(rc)
            return rc;
        blob = filedata;
        blob_len = filedata_len;
    }
    if(!blob || !blob_len) {
        rc = ssh2_err(session, LIBSSH2_ERROR_FILE, "The key file is empty");
        goto out;
    }

    if(contains(blob, blob_len, OPENSSH_HEADER)) {
        /* ssh-keygen's own format: decrypted by libssh2 (bcrypt_pbkdf and the cipher it names, AES here). */
        char *name = NULL;
        size_t name_len = 0;
        rc = ssh2_openssh_pem_parse(session, NULL, blob, blob_len, passphrase, &key->decrypted);
        if(rc)
            goto out;
        rc = -1;
        if(ssh2_get_chars(key->decrypted, &name, &name_len)) {
            rc = ssh2_err(session, LIBSSH2_ERROR_FILE, "The key file is not valid");
            goto out;
        }
        if(name_len == 7 && !memcmp(name, "ssh-rsa", 7)) {
            /* n, e, d, iqmp, p, q */
            unsigned char *v[6];
            size_t l[6];
            int k;
            for(k = 0; k < 6; ++k) {
                if(ssh2_get_bignum_bytes(key->decrypted, &v[k], &l[k]) || !l[k]) {
                    rc = ssh2_err(session, LIBSSH2_ERROR_FILE, "The RSA key file is not valid");
                    goto out;
                }
            }
            key->n = v[0];
            key->n_len = l[0];
            key->e = v[1];
            key->e_len = l[1];
            key->d = v[2];
            key->d_len = l[2];
            key->qinv = v[3];
            key->qinv_len = l[3];
            key->p = v[4];
            key->p_len = l[4];
            key->q = v[5];
            key->q_len = l[5];
            key->type = PRIV_RSA;
        }
        else if(name_len == 11 && !memcmp(name, "ssh-ed25519", 11)) {
            /* The public key, then the seed followed by the public key again. */
            unsigned char *pub = NULL, *priv = NULL;
            size_t pub_len = 0, priv_len = 0;
            if(ssh2_get_string(key->decrypted, &pub, &pub_len) ||
               ssh2_get_string(key->decrypted, &priv, &priv_len) || pub_len != SSH2_ED25519_KEY_LEN ||
               priv_len != SSH2_ED25519_PRIVATE_KEY_LEN) {
                rc = ssh2_err(session, LIBSSH2_ERROR_FILE, "The Ed25519 key file is not valid");
                goto out;
            }
            key->ed_pub = pub;
            key->ed_seed = priv;
            key->type = PRIV_ED25519;
        }
        else {
            rc = ssh2_err(session, LIBSSH2_ERROR_METHOD_NOT_SUPPORTED,
                          "Only RSA and Ed25519 keys are supported (not ECDSA nor DSA)");
            goto out;
        }
        rc = 0;
    }
    else if(contains(blob, blob_len, PEM_RSA_HEADER)) {
        /* RSAPrivateKey ::= SEQUENCE { version, n, e, d, p, q, dp, dq, qinv } (RFC 8017, A.1.2). An encrypted one
           needs a CBC cipher, which is not here. */
        unsigned char *data;
        size_t len;
        const unsigned char *version;
        size_t version_len;
        if(contains(blob, blob_len, "Proc-Type: 4,ENCRYPTED")) {
            rc = ssh2_err(session, LIBSSH2_ERROR_METHOD_NOT_SUPPORTED,
                          "This old encrypted key format is not supported: convert the key to the OpenSSH format "
                          "with ssh-keygen -p -f <key file>");
            goto out;
        }
        if(ssh2_pem_parse(session, PEM_RSA_HEADER, PEM_RSA_FOOTER, NULL, blob, blob_len, passphrase, &key->der,
                          &key->der_len, NULL)) {
            key->der = NULL;
            rc = ssh2_err(session, LIBSSH2_ERROR_FILE, "The RSA key file is not valid");
            goto out;
        }
        data = key->der;
        len = key->der_len;
        if(ssh2_pem_decode_sequence(&data, &len) || der_next(&data, &len, &version, &version_len) ||
           der_next(&data, &len, &key->n, &key->n_len) || der_next(&data, &len, &key->e, &key->e_len) ||
           !key->n_len || !key->e_len) {
            rc = ssh2_err(session, LIBSSH2_ERROR_FILE, "The RSA key file is not valid");
            goto out;
        }
        key->type = PRIV_RSA;
        rc = 0;
    }
    else if(contains(blob, blob_len, "-----BEGIN ") && contains(blob, blob_len, "PRIVATE KEY-----")) {
        rc = ssh2_err(session, LIBSSH2_ERROR_METHOD_NOT_SUPPORTED,
                      "This key format is not supported: convert the key to the OpenSSH format with "
                      "ssh-keygen -p -f <key file>");
    }
    else {
        rc = ssh2_err(session, LIBSSH2_ERROR_FILE, "This is not a private key file");
    }

out:
    if(rc)
        priv_key_free(session, key);
    if(filedata)
        ssh2_zero_free(session, filedata, filedata_len + 1);
    return rc;
}

/* r = a mod m, big-endian numbers without leading zeros, r as long as m, `work` m_len + 1 bytes. Bit by bit, as Mbed
   TLS computes these numbers too (mbedtls_rsa_deduce_crt): not in constant time, once for each authentication. */
static void bn_mod(unsigned char *r, const unsigned char *a, size_t a_len, const unsigned char *m, size_t m_len,
                   unsigned char *work)
{
    size_t i, k;
    int bit;
    memset(work, 0, m_len + 1);
    for(i = 0; i < a_len; ++i) {
        for(bit = 7; bit >= 0; --bit) {
            int ge = 1, borrow = 0;
            unsigned carry = (unsigned)(a[i] >> bit) & 1;
            /* The remainder so far, times two, plus the next bit of a: less than 2m, so m_len + 1 bytes. */
            for(k = m_len + 1; k-- > 0;) {
                unsigned v = ((unsigned)work[k] << 1) | carry;
                carry = v >> 8;
                work[k] = (unsigned char)v;
            }
            if(!work[0]) {
                for(k = 0; k < m_len; ++k) {
                    if(work[k + 1] != m[k]) {
                        ge = work[k + 1] > m[k];
                        break;
                    }
                }
            }
            if(!ge)
                continue;
            for(k = m_len + 1; k-- > 0;) {
                int v = (int)work[k] - (k ? m[k - 1] : 0) - borrow;
                borrow = v < 0;
                work[k] = (unsigned char)(v + (borrow ? 256 : 0));
            }
        }
    }
    memcpy(r, work + 1, m_len);
}

/* The RSAPrivateKey PSA imports, from the numbers of an OpenSSH key, which leaves out d mod (p - 1) and
   d mod (q - 1). */
static int rsa_der(LIBSSH2_SESSION *session, const struct priv_key *key, unsigned char **out, size_t *out_len)
{
    unsigned char *pm1 = NULL, *qm1 = NULL, *dp = NULL, *dq = NULL, *work = NULL, *der = NULL, *at;
    const unsigned char *dp_v, *dq_v;
    size_t dp_len, dq_len, body, total = 0, work_len;
    static const unsigned char zero = 0;
    int rc = -1;

    /* p and q are odd primes: minus one, only their last byte changes. */
    if(!(key->p[key->p_len - 1] & 1) || !(key->q[key->q_len - 1] & 1) || key->p_len < 2 || key->q_len < 2)
        return ssh2_err(session, LIBSSH2_ERROR_FILE, "The RSA key file is not valid");
    work_len = (key->p_len > key->q_len ? key->p_len : key->q_len) + 1;
    pm1 = SSH2_ALLOC(session, key->p_len);
    qm1 = SSH2_ALLOC(session, key->q_len);
    dp = SSH2_ALLOC(session, key->p_len);
    dq = SSH2_ALLOC(session, key->q_len);
    work = SSH2_ALLOC(session, work_len);
    if(!pm1 || !qm1 || !dp || !dq || !work)
        goto out;
    memcpy(pm1, key->p, key->p_len);
    pm1[key->p_len - 1] -= 1;
    memcpy(qm1, key->q, key->q_len);
    qm1[key->q_len - 1] -= 1;
    bn_mod(dp, key->d, key->d_len, pm1, key->p_len, work);
    bn_mod(dq, key->d, key->d_len, qm1, key->q_len, work);
    dp_len = key->p_len;
    dq_len = key->q_len;
    dp_v = skip_zeros(dp, &dp_len);
    dq_v = skip_zeros(dq, &dq_len);
    if(!dp_len || !dq_len)
        goto out;

    body = der_integer(NULL, &zero, 1) + der_integer(NULL, key->n, key->n_len) +
           der_integer(NULL, key->e, key->e_len) + der_integer(NULL, key->d, key->d_len) +
           der_integer(NULL, key->p, key->p_len) + der_integer(NULL, key->q, key->q_len) +
           der_integer(NULL, dp_v, dp_len) + der_integer(NULL, dq_v, dq_len) +
           der_integer(NULL, key->qinv, key->qinv_len);
    if(body > 0xFFFF)
        goto out;
    total = 4 + body;
    der = SSH2_ALLOC(session, total);
    if(!der)
        goto out;
    der[0] = 0x30;
    at = der_length(der + 1, body);
    /* The version, 0: an INTEGER of one zero byte (der_integer() takes numbers without leading zeros). */
    *at++ = 0x02;
    *at++ = 0x01;
    *at++ = 0x00;
    at += der_integer(at, key->n, key->n_len);
    at += der_integer(at, key->e, key->e_len);
    at += der_integer(at, key->d, key->d_len);
    at += der_integer(at, key->p, key->p_len);
    at += der_integer(at, key->q, key->q_len);
    at += der_integer(at, dp_v, dp_len);
    at += der_integer(at, dq_v, dq_len);
    at += der_integer(at, key->qinv, key->qinv_len);
    *out = der;
    *out_len = (size_t)(at - der);
    der = NULL;
    rc = 0;
out:
    if(pm1)
        ssh2_zero_free(session, pm1, key->p_len);
    if(qm1)
        ssh2_zero_free(session, qm1, key->q_len);
    if(dp)
        ssh2_zero_free(session, dp, key->p_len);
    if(dq)
        ssh2_zero_free(session, dq, key->q_len);
    if(work)
        ssh2_zero_free(session, work, work_len);
    if(der)
        ssh2_zero_free(session, der, total);
    return rc ? ssh2_err(session, LIBSSH2_ERROR_FILE, "The RSA key file is not valid") : 0;
}

int ssh2_rsa_new_priv(ssh2_rsa_ctx **rsa, LIBSSH2_SESSION *session, const char *filename, const char *blob,
                      size_t blob_len, const char *passphrase)
{
    struct priv_key key;
    unsigned char *der = NULL;
    size_t der_len = 0;
    ssh2_rsa_ctx *ctx;
    int own_der = 0;

    *rsa = NULL;
    if(priv_key_read(session, filename, blob, blob_len, passphrase, &key))
        return -1;
    if(key.type != PRIV_RSA) {
        priv_key_free(session, &key);
        return ssh2_err(session, LIBSSH2_ERROR_FILE, "The key file does not hold an RSA key");
    }
    if(key.der) {
        der = key.der;
        der_len = key.der_len;
    }
    else {
        if(rsa_der(session, &key, &der, &der_len)) {
            priv_key_free(session, &key);
            return -1;
        }
        own_der = 1;
    }
    ctx = SSH2_ALLOC(session, sizeof(*ctx));
    if(ctx) {
        ctx->bytes = key.n_len;
        ctx->key = psa_import(PSA_KEY_TYPE_RSA_KEY_PAIR, 0, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_ANY_HASH),
                              PSA_KEY_USAGE_SIGN_HASH, der, der_len);
    }
    if(own_der)
        ssh2_zero_free(session, der, der_len);
    priv_key_free(session, &key);
    if(!ctx)
        return -1;
    if(ctx->key == PSA_KEY_ID_NULL) {
        SSH2_FREE(session, ctx);
        return ssh2_err(session, LIBSSH2_ERROR_FILE, "The RSA key cannot be used (2048 to 4096 bits only)");
    }
    *rsa = ctx;
    return 0;
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
    psa_algorithm_t alg;
    unsigned char *sig;
    size_t n = 0;

    *signature = NULL;
    *signature_len = 0;
    if(hash_len == SSH2_SHA256_DIG_LEN)
        alg = PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256);
    else if(hash_len == SSH2_SHA512_DIG_LEN)
        alg = PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_512);
    else
        return -1;
    sig = SSH2_ALLOC(session, rsa->bytes);
    if(!sig)
        return -1;
    if(psa_sign_hash(rsa->key, alg, hash, hash_len, sig, rsa->bytes, &n) != PSA_SUCCESS) {
        SSH2_FREE(session, sig);
        return -1;
    }
    *signature = sig;
    *signature_len = n;
    return 0;
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
/* Ed25519: private keys, to sign with (ed25519.c); public keys, to verify, are not supported */

int ssh2_ed25519_new_public(ssh2_ed25519_ctx **ed_ctx, LIBSSH2_SESSION *session, const unsigned char *raw_pub_key,
                            const size_t key_len)
{
    (void)raw_pub_key;
    (void)key_len;
    *ed_ctx = NULL;
    return ssh2_err(session, LIBSSH2_ERROR_METHOD_NOT_SUPPORTED, "Ed25519 host keys are not supported");
}

int ssh2_ed25519_new_priv(ssh2_ed25519_ctx **ed_ctx, LIBSSH2_SESSION *session, const char *filename,
                          const char *blob, size_t blob_len, const char *passphrase)
{
    struct priv_key key;
    ssh2_ed25519_ctx *ctx;

    *ed_ctx = NULL;
    if(priv_key_read(session, filename, blob, blob_len, passphrase, &key))
        return -1;
    if(key.type != PRIV_ED25519) {
        priv_key_free(session, &key);
        return ssh2_err(session, LIBSSH2_ERROR_FILE, "The key file does not hold an Ed25519 key");
    }
    ctx = SSH2_ALLOC(session, sizeof(*ctx));
    if(ctx) {
        memcpy(ctx->seed, key.ed_seed, sizeof(ctx->seed));
        memcpy(ctx->pub, key.ed_pub, sizeof(ctx->pub));
    }
    priv_key_free(session, &key);
    if(!ctx)
        return -1;
    *ed_ctx = ctx;
    return 0;
}

int ssh2_ed25519_sign(ssh2_ed25519_ctx *ed_ctx, LIBSSH2_SESSION *session, uint8_t **out_sig, size_t *out_sig_len,
                      const uint8_t *message, size_t message_len)
{
    uint8_t *sig;

    *out_sig = NULL;
    *out_sig_len = 0;
    sig = SSH2_ALLOC(session, SSH2_ED25519_SIG_LEN);
    if(!sig)
        return -1;
    if(fcg_ed25519_sign(sig, message, message_len, ed_ctx->seed, ed_ctx->pub)) {
        SSH2_FREE(session, sig);
        return -1;
    }
    *out_sig = sig;
    *out_sig_len = SSH2_ED25519_SIG_LEN;
    return 0;
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
        ssh2_zero_free(session, ed_ctx, sizeof(*ed_ctx));
}

/*******************************************************************/
/* The public key of a private key file, as libssh2 sends it: the key's type and its SSH blob. Security keys are not
   supported (misc.c's ssh2_sk_pubkey() refuses them without LIBSSH2_KEY_SK). */

int ssh2_pub_privkey(LIBSSH2_SESSION *session, char **method, unsigned char **pubkeydata, size_t *pubkeydata_len,
                     const char *privkeyfile, const char *privkeyblob, size_t privkeyblob_len,
                     const char *passphrase)
{
    struct priv_key key;
    const char *name;
    size_t name_len, len;
    unsigned char *blob, *at;
    char *method_buf;
    int rc;

    *method = NULL;
    *pubkeydata = NULL;
    *pubkeydata_len = 0;
    /* libssh2's error code, which tells a passphrase missing or wrong (LIBSSH2_ERROR_KEYFILE_AUTH_FAILED). */
    rc = priv_key_read(session, privkeyfile, privkeyblob, privkeyblob_len, passphrase, &key);
    if(rc)
        return rc < 0 ? rc : -1;
    name = key.type == PRIV_RSA ? "ssh-rsa" : "ssh-ed25519";
    name_len = strlen(name);
    /* string type, then mpint e and mpint n, or string key; an mpint whose high bit is set takes a zero first. */
    if(key.type == PRIV_RSA)
        len = 4 + name_len + 4 + key.e_len + 1 + 4 + key.n_len + 1;
    else
        len = 4 + name_len + 4 + SSH2_ED25519_KEY_LEN;
    blob = SSH2_ALLOC(session, len);
    method_buf = SSH2_ALLOC(session, name_len + 1);
    if(!blob || !method_buf) {
        if(blob)
            SSH2_FREE(session, blob);
        if(method_buf)
            SSH2_FREE(session, method_buf);
        priv_key_free(session, &key);
        return -1;
    }
    memcpy(method_buf, name, name_len + 1);
    at = blob;
    ssh2_store_str(&at, name, name_len);
    if(key.type == PRIV_RSA) {
        ssh2_store_bignum_bytes(&at, key.e, key.e_len);
        ssh2_store_bignum_bytes(&at, key.n, key.n_len);
    }
    else
        ssh2_store_str(&at, key.ed_pub, SSH2_ED25519_KEY_LEN);
    priv_key_free(session, &key);
    *method = method_buf;
    *pubkeydata = blob;
    *pubkeydata_len = (size_t)(at - blob);
    return 0;
}
