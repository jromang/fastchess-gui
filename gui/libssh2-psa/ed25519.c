/* Ed25519 signatures: see ed25519.h. The field and group arithmetic follow TweetNaCl (public domain): numbers mod
 * 2^255 - 19 as 16 limbs of 16 bits, points in extended coordinates, the scalar multiplication a ladder of
 * conditional swaps whatever the bits of the secret. Shifts of negative numbers are written as multiplications, so
 * that nothing depends on what C leaves undefined.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ed25519.h"

#include <string.h>

#include <mbedtls/platform_util.h>
#include <psa/crypto.h>

typedef int64_t gf[16];

static const gf GF0 = {0};
static const gf GF1 = {1};
static const gf D2  = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
                       0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
static const gf BX  = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
                       0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
static const gf BY  = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
                       0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};

/* The order of the group, little-endian. */
static const int64_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                              0xa2, 0xde, 0xf9, 0xde, 0x14, 0,    0,    0,    0,    0,    0,
                              0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

static void set25519(gf r, const gf a)
{
    int i;
    for(i = 0; i < 16; ++i)
        r[i] = a[i];
}

static void car25519(gf o)
{
    int i;
    int64_t c;
    for(i = 0; i < 16; ++i) {
        o[i] += 65536;
        c = o[i] >> 16; /* arithmetic on every compiler the program is built with */
        if(i < 15)
            o[i + 1] += c - 1;
        else
            o[0] += 38 * (c - 1);
        o[i] -= c * 65536;
    }
}

/* Swaps p and q when b is 1, leaves them when 0, in the same time. */
static void sel25519(gf p, gf q, int b)
{
    int64_t t, c = ~((int64_t)b - 1);
    int i;
    for(i = 0; i < 16; ++i) {
        t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack25519(uint8_t *o, const gf n)
{
    int i, j, b;
    gf m, t;
    set25519(t, n);
    car25519(t);
    car25519(t);
    car25519(t);
    for(j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for(i = 1; i < 15; ++i) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for(i = 0; i < 16; ++i) {
        o[2 * i] = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)((t[i] >> 8) & 0xff);
    }
}

static uint8_t par25519(const gf a)
{
    uint8_t d[32];
    pack25519(d, a);
    return d[0] & 1;
}

static void add25519(gf o, const gf a, const gf b)
{
    int i;
    for(i = 0; i < 16; ++i)
        o[i] = a[i] + b[i];
}

static void sub25519(gf o, const gf a, const gf b)
{
    int i;
    for(i = 0; i < 16; ++i)
        o[i] = a[i] - b[i];
}

static void mul25519(gf o, const gf a, const gf b)
{
    int64_t t[31];
    int i, j;
    for(i = 0; i < 31; ++i)
        t[i] = 0;
    for(i = 0; i < 16; ++i)
        for(j = 0; j < 16; ++j)
            t[i + j] += a[i] * b[j];
    for(i = 0; i < 15; ++i)
        t[i] += 38 * t[i + 16];
    for(i = 0; i < 16; ++i)
        o[i] = t[i];
    car25519(o);
    car25519(o);
}

static void inv25519(gf o, const gf in)
{
    gf c;
    int a;
    set25519(c, in);
    for(a = 253; a >= 0; --a) {
        mul25519(c, c, c);
        if(a != 2 && a != 4)
            mul25519(c, c, in);
    }
    set25519(o, c);
}

/* p += q, in extended coordinates (X, Y, Z, T). */
static void point_add(gf p[4], gf q[4])
{
    gf a, b, c, d, t, e, f, g, h;
    sub25519(a, p[1], p[0]);
    sub25519(t, q[1], q[0]);
    mul25519(a, a, t);
    add25519(b, p[0], p[1]);
    add25519(t, q[0], q[1]);
    mul25519(b, b, t);
    mul25519(c, p[3], q[3]);
    mul25519(c, c, D2);
    mul25519(d, p[2], q[2]);
    add25519(d, d, d);
    sub25519(e, b, a);
    sub25519(f, d, c);
    add25519(g, d, c);
    add25519(h, b, a);
    mul25519(p[0], e, f);
    mul25519(p[1], h, g);
    mul25519(p[2], g, f);
    mul25519(p[3], e, h);
}

static void point_swap(gf p[4], gf q[4], int b)
{
    int i;
    for(i = 0; i < 4; ++i)
        sel25519(p[i], q[i], b);
}

static void point_pack(uint8_t *r, gf p[4])
{
    gf tx, ty, zi;
    inv25519(zi, p[2]);
    mul25519(tx, p[0], zi);
    mul25519(ty, p[1], zi);
    pack25519(r, ty);
    r[31] ^= (uint8_t)(par25519(tx) << 7);
}

/* p = s * B, the base point, s a 256-bit little-endian scalar. */
static void scalar_base(gf p[4], const uint8_t *s)
{
    gf q[4];
    int i;
    set25519(q[0], BX);
    set25519(q[1], BY);
    set25519(q[2], GF1);
    mul25519(q[3], BX, BY);
    set25519(p[0], GF0);
    set25519(p[1], GF1);
    set25519(p[2], GF1);
    set25519(p[3], GF0);
    for(i = 255; i >= 0; --i) {
        int b = (s[i / 8] >> (i & 7)) & 1;
        point_swap(p, q, b);
        point_add(q, p);
        point_add(p, p);
        point_swap(p, q, b);
    }
    mbedtls_platform_zeroize(q, sizeof(q));
}

/* r = x mod L, x 64 limbs of a byte each (signed, as the products leave them). */
static void mod_l(uint8_t *r, int64_t x[64])
{
    int64_t carry;
    int i, j;
    for(i = 63; i >= 32; --i) {
        carry = 0;
        for(j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry * 256;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for(j = 0; j < 32; ++j) {
        x[j] += carry - (x[31] >> 4) * L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for(j = 0; j < 32; ++j)
        x[j] -= carry * L[j];
    for(i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        r[i] = (uint8_t)(x[i] & 255);
    }
}

/* A 64-byte hash, reduced mod L into its first 32 bytes. */
static void reduce(uint8_t *r)
{
    int64_t x[64];
    int i;
    for(i = 0; i < 64; ++i)
        x[i] = r[i];
    for(i = 0; i < 64; ++i)
        r[i] = 0;
    mod_l(r, x);
    mbedtls_platform_zeroize(x, sizeof(x));
}

/* SHA-512 of a || b. */
static int sha512(uint8_t out[64], const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len,
                  const uint8_t *c, size_t c_len)
{
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    size_t n = 0;
    int ok = psa_crypto_init() == PSA_SUCCESS && psa_hash_setup(&op, PSA_ALG_SHA_512) == PSA_SUCCESS &&
             (!a_len || psa_hash_update(&op, a, a_len) == PSA_SUCCESS) &&
             (!b_len || psa_hash_update(&op, b, b_len) == PSA_SUCCESS) &&
             (!c_len || psa_hash_update(&op, c, c_len) == PSA_SUCCESS) &&
             psa_hash_finish(&op, out, 64, &n) == PSA_SUCCESS && n == 64;
    if(!ok)
        (void)psa_hash_abort(&op);
    return ok ? 0 : -1;
}

/* The secret scalar and the prefix of RFC 8032, 5.1.5: SHA-512 of the seed, its first half clamped. */
static int expand(uint8_t d[64], const uint8_t seed[32])
{
    if(sha512(d, seed, 32, NULL, 0, NULL, 0))
        return -1;
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64;
    return 0;
}

int fcg_ed25519_public(uint8_t pub[32], const uint8_t seed[32])
{
    uint8_t d[64];
    gf p[4];
    if(expand(d, seed))
        return -1;
    scalar_base(p, d);
    point_pack(pub, p);
    mbedtls_platform_zeroize(d, sizeof(d));
    mbedtls_platform_zeroize(p, sizeof(p));
    return 0;
}

int fcg_ed25519_sign(uint8_t sig[64], const uint8_t *m, size_t m_len, const uint8_t seed[32], const uint8_t pub[32])
{
    uint8_t d[64], r[64], h[64];
    int64_t x[64];
    gf p[4];
    int i, j, rc = -1;

    if(expand(d, seed))
        goto out;
    /* r = SHA-512(prefix || M) mod L; R = rB. */
    if(sha512(r, d + 32, 32, m, m_len, NULL, 0))
        goto out;
    reduce(r);
    scalar_base(p, r);
    point_pack(sig, p);
    /* S = (r + SHA-512(R || A || M) * s) mod L. */
    if(sha512(h, sig, 32, pub, 32, m, m_len))
        goto out;
    reduce(h);
    for(i = 0; i < 64; ++i)
        x[i] = 0;
    for(i = 0; i < 32; ++i)
        x[i] = r[i];
    for(i = 0; i < 32; ++i)
        for(j = 0; j < 32; ++j)
            x[i + j] += (int64_t)h[i] * d[j];
    mod_l(sig + 32, x);
    rc = 0;
out:
    mbedtls_platform_zeroize(d, sizeof(d));
    mbedtls_platform_zeroize(r, sizeof(r));
    mbedtls_platform_zeroize(x, sizeof(x));
    mbedtls_platform_zeroize(p, sizeof(p));
    return rc;
}
