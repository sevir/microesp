/* CLI access policy, provisioning window and log redaction (security hardening). */
#include <string.h>

#include "cli_policy.h"
#include "log_redact.h"
#include "unity.h"

static void test_release_cli_only_safe_commands(void)
{
    clip_ctx_t c = {.dev = false, .creds_provisioned = true, .pid_provisioned = true, .prov_window = false};
    const char *ok[] = {"!help", "!status", "!version", "!dp", "!log", "!dfu", "!usj", "!reboot", "!cancel"};
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) TEST_ASSERT_EQUAL(CLIP_ALLOW, clip_check(ok[i], &c));
    const char *dev[] = {"!pair", "!unpair", "!wake", "!method", "!countdown", "!cmd", "!key", "!reset-tuya"};
    for (size_t i = 0; i < sizeof(dev) / sizeof(dev[0]); i++)
        TEST_ASSERT_EQUAL(CLIP_DEV_ONLY, clip_check(dev[i], &c));
    TEST_ASSERT_EQUAL(CLIP_LOCKED, clip_check("!auth", &c));
    TEST_ASSERT_EQUAL(CLIP_LOCKED, clip_check("!pid", &c));
    TEST_ASSERT_EQUAL(CLIP_UNKNOWN, clip_check("!nope", &c));
    TEST_ASSERT_EQUAL(CLIP_UNKNOWN, clip_check("!statusx", &c));
    TEST_ASSERT_EQUAL(CLIP_UNKNOWN, clip_check(NULL, &c));
    /* the window opens provisioning only, never dev commands */
    c.prov_window = true;
    TEST_ASSERT_EQUAL(CLIP_ALLOW, clip_check("!auth", &c));
    TEST_ASSERT_EQUAL(CLIP_ALLOW, clip_check("!pid", &c));
    TEST_ASSERT_EQUAL(CLIP_DEV_ONLY, clip_check("!pair", &c));
    TEST_ASSERT_EQUAL(CLIP_DEV_ONLY, clip_check("!reset-tuya", &c));
}

static void test_release_first_provisioning_per_item(void)
{
    clip_ctx_t c = {.dev = false, .creds_provisioned = false, .pid_provisioned = false};
    TEST_ASSERT_EQUAL(CLIP_ALLOW, clip_check("!auth", &c));
    TEST_ASSERT_EQUAL(CLIP_ALLOW, clip_check("!pid", &c));
    c.creds_provisioned = true; /* after !auth: the PID can still be set once */
    TEST_ASSERT_EQUAL(CLIP_LOCKED, clip_check("!auth", &c));
    TEST_ASSERT_EQUAL(CLIP_ALLOW, clip_check("!pid", &c));
    c.pid_provisioned = true;
    TEST_ASSERT_EQUAL(CLIP_LOCKED, clip_check("!pid", &c));
}

static void test_dev_cli_allows_everything_known(void)
{
    clip_ctx_t c = {.dev = true, .creds_provisioned = true, .pid_provisioned = true};
    const char *all[] = {"!status", "!auth", "!pid", "!pair", "!unpair", "!wake", "!cmd", "!key", "!reset-tuya"};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) TEST_ASSERT_EQUAL(CLIP_ALLOW, clip_check(all[i], &c));
    TEST_ASSERT_EQUAL(CLIP_UNKNOWN, clip_check("!x", &c));
}

static void test_provisioning_window(void)
{
    clip_window_t w = {0};
    TEST_ASSERT_FALSE(clip_window_active(&w, 0));
    TEST_ASSERT_EQUAL(0, clip_window_remaining_s(&w, 0));
    uint32_t t0 = 0xFFFFF000u; /* across the 32-bit millisecond wrap */
    clip_window_open(&w, t0);
    TEST_ASSERT_TRUE(clip_window_active(&w, t0 + 1));
    TEST_ASSERT_EQUAL(120, clip_window_remaining_s(&w, t0));
    TEST_ASSERT_TRUE(clip_window_active(&w, t0 + CLIP_PROV_WINDOW_MS - 1));
    TEST_ASSERT_EQUAL(1, clip_window_remaining_s(&w, t0 + CLIP_PROV_WINDOW_MS - 1));
    TEST_ASSERT_FALSE(clip_window_active(&w, t0 + CLIP_PROV_WINDOW_MS));
    TEST_ASSERT_FALSE(clip_window_active(&w, t0 + 10)); /* stays closed */
    clip_window_open(&w, 100);
    clip_window_close(&w);
    TEST_ASSERT_FALSE(clip_window_active(&w, 200));
}

static void test_log_redaction(void)
{
    lr_set_secret(LR_SLOT_TUYA_AUTHKEY, "Zx9Kq2Lm8Np4Rs6Tv1Wy3Ab5Cd7Ef0Gh");
    lr_set_secret(LR_SLOT_PAIR_CODE, "042917");
    TEST_ASSERT_TRUE(lr_sensitive("[01-01 00:00:00 ty D][x.c:1] key Zx9Kq2Lm8Np4Rs6Tv1Wy3Ab5Cd7Ef0Gh(32)\n"));
    TEST_ASSERT_TRUE(lr_sensitive("pairing code: 042917\n"));
    TEST_ASSERT_TRUE(lr_sensitive("authKey:abc"));
    TEST_ASSERT_TRUE(lr_sensitive("{\"localKey\":\"...\"}"));
    TEST_ASSERT_TRUE(lr_sensitive("POST JSON: {\"token\":\"x\"}"));
    TEST_ASSERT_TRUE(lr_sensitive("REGIST_KEY: x"));
    TEST_ASSERT_FALSE(lr_sensitive("agent: session ready (#1)\n"));
    TEST_ASSERT_FALSE(lr_sensitive("agent pairing mode ON (button 3 s) for 120 s\n"));
    TEST_ASSERT_FALSE(lr_sensitive("agent: PAIRED, new key stored\n"));
    TEST_ASSERT_FALSE(lr_sensitive(NULL));
    TEST_ASSERT_EQUAL_STRING(LR_REDACTED, lr_filter("code 042917"));
    TEST_ASSERT_EQUAL_STRING("ok\n", lr_filter("ok\n"));
    /* cleared / too short secrets are not matched */
    lr_set_secret(LR_SLOT_PAIR_CODE, NULL);
    TEST_ASSERT_FALSE(lr_sensitive("heap 042917"));
    lr_set_secret(LR_SLOT_PAIR_CODE, "12345");
    TEST_ASSERT_FALSE(lr_sensitive("12345"));
    lr_set_secret(LR_SLOT_TUYA_AUTHKEY, "");
    TEST_ASSERT_FALSE(lr_sensitive("Zx9Kq2Lm8Np4Rs6Tv1Wy3Ab5Cd7Ef0Gh"));
    lr_set_secret(99, "ignored-slot");
}

void run_security_tests(void)
{
    RUN_TEST(test_release_cli_only_safe_commands);
    RUN_TEST(test_release_first_provisioning_per_item);
    RUN_TEST(test_dev_cli_allows_everything_known);
    RUN_TEST(test_provisioning_window);
    RUN_TEST(test_log_redaction);
}
