#include "mesp_crypto.h"

#include <string.h>

#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"

int mc_hmac_sha256(const uint8_t *key, size_t klen, const void *msg, size_t mlen, uint8_t out[32])
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md) return -1;
    return mbedtls_md_hmac(md, key, klen, (const unsigned char *)msg, mlen, out) == 0 ? 0 : -1;
}

int mc_hkdf_sha256(const uint8_t *ikm, size_t ikm_len, const uint8_t *salt, size_t salt_len, const uint8_t *info,
                   size_t info_len, uint8_t *out, size_t out_len)
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md) return -1;
    return mbedtls_hkdf(md, salt, salt_len, ikm, ikm_len, info, info_len, out, out_len) == 0 ? 0 : -1;
}

void mc_hex(const uint8_t *in, size_t n, char *out)
{
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = d[in[i] >> 4];
        out[2 * i + 1] = d[in[i] & 15];
    }
    out[2 * n] = 0;
}

static int nib(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool mc_is_hex(const char *s, size_t len)
{
    if (!s) return false;
    for (size_t i = 0; i < len; i++)
        if (nib(s[i]) < 0) return false;
    return s[len] == 0;
}

int mc_unhex(const char *hex, uint8_t *out, size_t n)
{
    if (!mc_is_hex(hex, 2 * n)) return -1;
    for (size_t i = 0; i < n; i++) out[i] = (uint8_t)(nib(hex[2 * i]) << 4 | nib(hex[2 * i + 1]));
    return 0;
}

bool mc_ct_eq(const void *a, const void *b, size_t n)
{
    const volatile uint8_t *x = a, *y = b;
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= x[i] ^ y[i];
    return acc == 0;
}

void mc_sign(const uint8_t key[MC_KEY_LEN], const char *msg, char out[MC_SIG_HEX + 1])
{
    uint8_t mac[32];
    if (mc_hmac_sha256(key, MC_KEY_LEN, msg, strlen(msg), mac) != 0) memset(mac, 0, sizeof(mac));
    mc_hex(mac, 32, out);
    memset(mac, 0, sizeof(mac));
}

bool mc_verify(const uint8_t key[MC_KEY_LEN], const char *msg, const char *sig_hex)
{
    uint8_t want[32], got[32];
    if (mc_unhex(sig_hex, got, 32) != 0) return false;
    if (mc_hmac_sha256(key, MC_KEY_LEN, msg, strlen(msg), want) != 0) return false;
    bool ok = mc_ct_eq(want, got, 32);
    memset(want, 0, sizeof(want));
    return ok;
}

int mc_derive_pair_key(const char *code, const char *na_hex, const char *nd_hex, uint8_t key[MC_KEY_LEN])
{
    uint8_t salt[2 * MC_NONCE_LEN];
    if (!code || strlen(code) != 6) return -1;
    for (int i = 0; i < 6; i++)
        if (code[i] < '0' || code[i] > '9') return -1;
    if (mc_unhex(na_hex, salt, MC_NONCE_LEN) != 0) return -1;
    if (mc_unhex(nd_hex, salt + MC_NONCE_LEN, MC_NONCE_LEN) != 0) return -1;
    return mc_hkdf_sha256((const uint8_t *)code, 6, salt, sizeof(salt), (const uint8_t *)MC_PAIR_INFO,
                          strlen(MC_PAIR_INFO), key, MC_KEY_LEN);
}
