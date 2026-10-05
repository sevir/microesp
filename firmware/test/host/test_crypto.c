#include <string.h>

#include "helpers.h"
#include "mesp_crypto.h"
#include "unity.h"

static void test_pair_key_derivation_vector(void)
{
    uint8_t k[32], want[32];
    TEST_ASSERT_EQUAL(0, mc_derive_pair_key(vstr("pairing", "code"), vstr("pairing", "agent_nonce"),
                                            vstr("pairing", "dongle_nonce"), k));
    hex2bin(vstr("pairing", "key_hex"), want, 32);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, k, 32);
}

static void test_pair_sigs_vector(void)
{
    uint8_t k[32];
    char msg[64], sig[65];
    const char *na = vstr("pairing", "agent_nonce"), *nd = vstr("pairing", "dongle_nonce");
    hex2bin(vstr("pairing", "key_hex"), k, 32);
    snprintf(msg, sizeof(msg), "pair|%s|%s", na, nd);
    mc_sign(k, msg, sig);
    TEST_ASSERT_EQUAL_STRING(vstr("pairing", "pair_confirm_sig"), sig);
    snprintf(msg, sizeof(msg), "pair_ok|%s|%s", nd, na);
    mc_sign(k, msg, sig);
    TEST_ASSERT_EQUAL_STRING(vstr("pairing", "pair_ok_sig"), sig);
}

static void test_session_sigs_vector(void)
{
    uint8_t k[32];
    char msg[96], sig[65];
    const char *na = vstr("session", "agent_nonce"), *nd = vstr("session", "dongle_nonce");
    hex2bin(vstr("session", "key_hex"), k, 32);
    snprintf(msg, sizeof(msg), "welcome|%s|%s", na, nd);
    mc_sign(k, msg, sig);
    TEST_ASSERT_EQUAL_STRING(vstr("session", "welcome_sig"), sig);
    snprintf(msg, sizeof(msg), "auth|%s|%s", nd, na);
    TEST_ASSERT_TRUE(mc_verify(k, msg, vstr("session", "auth_sig")));
    cJSON *cmds = cJSON_GetObjectItem(cJSON_GetObjectItem(vectors(), "session"), "cmds");
    cJSON *c;
    int n = 0;
    cJSON_ArrayForEach(c, cmds)
    {
        snprintf(msg, sizeof(msg), "cmd|%d|%s|%s|%s", cJSON_GetObjectItem(c, "id")->valueint,
                 cJSON_GetObjectItem(c, "action")->valuestring, na, nd);
        mc_sign(k, msg, sig);
        TEST_ASSERT_EQUAL_STRING(cJSON_GetObjectItem(c, "sig")->valuestring, sig);
        n++;
    }
    TEST_ASSERT_EQUAL(3, n); /* shutdown, reboot, script:backup */
}

static void test_bad_sig_rejected(void)
{
    uint8_t k[32];
    hex2bin(vstr("session", "key_hex"), k, 32);
    cJSON *b = cJSON_GetObjectItem(vectors(), "cmd_bad_sig");
    char msg[96];
    snprintf(msg, sizeof(msg), "cmd|%d|%s|%s|%s", cJSON_GetObjectItem(b, "id")->valueint,
             cJSON_GetObjectItem(b, "action")->valuestring, vstr("session", "agent_nonce"),
             vstr("session", "dongle_nonce"));
    TEST_ASSERT_FALSE(mc_verify(k, msg, cJSON_GetObjectItem(b, "sig")->valuestring));
    /* malformed signatures */
    TEST_ASSERT_FALSE(mc_verify(k, msg, ""));
    TEST_ASSERT_FALSE(mc_verify(k, msg, "00"));
    char upper[65];
    snprintf(upper, sizeof(upper), "%s", vstr("session", "welcome_sig"));
    for (char *p = upper; *p; p++)
        if (*p >= 'a' && *p <= 'f') *p -= 32;
    snprintf(msg, sizeof(msg), "welcome|%s|%s", vstr("session", "agent_nonce"), vstr("session", "dongle_nonce"));
    TEST_ASSERT_FALSE_MESSAGE(mc_verify(k, msg, upper), "uppercase hex must be rejected");
    TEST_ASSERT_TRUE(mc_verify(k, msg, vstr("session", "welcome_sig")));
}

static void test_hex_helpers(void)
{
    uint8_t b[3];
    char s[7];
    TEST_ASSERT_EQUAL(0, mc_unhex("a1ff00", b, 3));
    mc_hex(b, 3, s);
    TEST_ASSERT_EQUAL_STRING("a1ff00", s);
    TEST_ASSERT_NOT_EQUAL(0, mc_unhex("A1FF00", b, 3));
    TEST_ASSERT_NOT_EQUAL(0, mc_unhex("a1ff0", b, 3));
    TEST_ASSERT_NOT_EQUAL(0, mc_unhex("a1ff000", b, 3));
    TEST_ASSERT_TRUE(mc_is_hex("0011223344556677", 16));
    TEST_ASSERT_FALSE(mc_is_hex("001122334455667", 16));
    TEST_ASSERT_FALSE(mc_is_hex(NULL, 16));
}

static void test_derive_rejects_bad_input(void)
{
    uint8_t k[32];
    TEST_ASSERT_NOT_EQUAL(0, mc_derive_pair_key("12345", "a1b2c3d4e5f60718", "0f1e2d3c4b5a6978", k));
    TEST_ASSERT_NOT_EQUAL(0, mc_derive_pair_key("12345a", "a1b2c3d4e5f60718", "0f1e2d3c4b5a6978", k));
    TEST_ASSERT_NOT_EQUAL(0, mc_derive_pair_key("123456", "a1b2c3d4e5f6071", "0f1e2d3c4b5a6978", k));
    TEST_ASSERT_NOT_EQUAL(0, mc_derive_pair_key("123456", "a1b2c3d4e5f60718", "XX1e2d3c4b5a6978", k));
}

void run_crypto_tests(void)
{
    RUN_TEST(test_pair_key_derivation_vector);
    RUN_TEST(test_pair_sigs_vector);
    RUN_TEST(test_session_sigs_vector);
    RUN_TEST(test_bad_sig_rejected);
    RUN_TEST(test_hex_helpers);
    RUN_TEST(test_derive_rejects_bad_input);
}
