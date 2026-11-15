/* Ed25519 signatures (RFC 8032) for the SSH keys of the remote machines: PSA has no EdDSA, so the curve arithmetic
 * is here, after TweetNaCl (Bernstein, van Gastel, Janssen, Lange, Schwabe, Smetsers; public domain), its hash
 * SHA-512 through PSA. Signing only, in constant time as TweetNaCl is: the client signs the authentication with the
 * user's key, it never checks an Ed25519 signature (host keys are RSA, see mbedtls_psa.h).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef FCG_ED25519_H
#define FCG_ED25519_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The public key of the private key `seed` (the 32 bytes RFC 8032 calls the private key). 0 on success. */
int fcg_ed25519_public(uint8_t pub[32], const uint8_t seed[32]);

/* The signature of `m` by the key `seed`, whose public key is `pub`. 0 on success, -1 when SHA-512 fails. */
int fcg_ed25519_sign(uint8_t sig[64], const uint8_t *m, size_t m_len, const uint8_t seed[32], const uint8_t pub[32]);

#ifdef __cplusplus
}
#endif

#endif /* FCG_ED25519_H */
