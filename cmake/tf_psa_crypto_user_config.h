/* Added to TF-PSA-Crypto's default configuration (TF_PSA_CRYPTO_USER_CONFIG_FILE).
 *
 * Several threads use the cryptography at once (the lichess relay, the SSH tunnel), which PSA allows only with its
 * threading layer: pthread, also on Windows (mingw-w64's winpthreads). */
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD
