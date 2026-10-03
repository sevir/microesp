/*
 * MicroESP — crypto helpers for the cdc-v1 protocol (HMAC-SHA256, HKDF-SHA256).
 * Pure C on top of mbedTLS: builds on the target (ESP-IDF mbedTLS) and on the host
 * (test/host compiles the same mbedTLS sources).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MC_KEY_LEN    32
#define MC_SIG_HEX    64 /* hex chars of an HMAC-SHA256 signature */
#define MC_NONCE_LEN  8
#define MC_NONCE_HEX  16
#define MC_PAIR_INFO  "microesp-pair-v1"

/* HMAC-SHA256(key, msg). Returns 0 on success. */
int mc_hmac_sha256(const uint8_t *key, size_t klen, const void *msg, size_t mlen, uint8_t out[32]);

/* HKDF-SHA256 (RFC 5869). Returns 0 on success. */
int mc_hkdf_sha256(const uint8_t *ikm, size_t ikm_len, const uint8_t *salt, size_t salt_len, const uint8_t *info,
                   size_t info_len, uint8_t *out, size_t out_len);

/* Lowercase hex encode; out must hold 2n+1 chars. */
void mc_hex(const uint8_t *in, size_t n, char *out);

/* Strict decode: exactly 2n lowercase hex chars. Returns 0 on success. */
int mc_unhex(const char *hex, uint8_t *out, size_t n);

/* true if s is exactly `len` lowercase hex chars. */
bool mc_is_hex(const char *s, size_t len);

/* Constant-time comparison. */
bool mc_ct_eq(const void *a, const void *b, size_t n);

/* sig = hex(HMAC-SHA256(K, msg)); out holds 65 chars. */
void mc_sign(const uint8_t key[MC_KEY_LEN], const char *msg, char out[MC_SIG_HEX + 1]);

/* Verify a hex signature in constant time (format checked first). */
bool mc_verify(const uint8_t key[MC_KEY_LEN], const char *msg, const char *sig_hex);

/* K = HKDF-SHA256(ikm=code ascii, salt=bytes(na)||bytes(nd), info="microesp-pair-v1", L=32). 0 on success. */
int mc_derive_pair_key(const char *code, const char *na_hex, const char *nd_hex, uint8_t key[MC_KEY_LEN]);

#ifdef __cplusplus
}
#endif
