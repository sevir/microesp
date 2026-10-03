/* CLI access policy, provisioning window and log redaction (security hardening). */
#include <string.h>

#include "cli_policy.h"
#include "log_redact.h"
#include "unity.h"

static void test_release_cli_only_safe_commands(void)
{
    clip_ctx_t c = {.dev = false, .tylink_provisioned = true, .wifi_provisioned = true, .prov_window = false};
    const char *ok[] = {"!help", "!status", "!version", "!dp", "!log", "!dfu", "!usj", "!reboot", "!cancel"};
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) TEST_ASSERT_EQUAL(CLIP_ALLOW, clip_check(ok[i], &c));
    const char *dev[] = {"!pair", "!unpair", "!wake", "!method", "!countdown", "!cmd", "!key"};
    for (size_t i = 0; i < sizeof(dev) / sizeof(dev[0]); i++)
        TEST_ASSERT_EQUAL(CLIP_DEV_ONLY, clip_check(dev[i], &c));
    TEST_ASSERT_EQUAL(CLIP_LOCKED, clip_check("!tylink", &c));
    TEST_ASSERT_EQUAL(CLIP_LOCKED, clip_check("!wifi", &c));
    TEST_ASSERT_EQUAL(CLIP_UNKNOWN, clip_check("!nope", &c));
    TEST_ASSERT_EQUAL(CLIP_UNKNOWN, clip_check("!statusx", &c));
    TEST_ASSERT_EQUAL(CLIP_UNKNOWN, clip_check(NULL, &c));
    /* the window opens provisioning only, never dev commands */
    c.prov_window = true;
    TEST_ASSERT_EQUAL(CLIP_ALLOW, clip_check("!tylink", &c));
    TEST_ASSERT_EQUAL(CLIP_ALLOW, clip_check("!wifi", &c));
    TEST_ASSERT_EQUAL(CLIP_DEV_ONLY, clip_check("!pair", &c));
    TEST_ASSERT_EQUAL(CLIP_DEV_ONLY, clip_check("!key", &c));
}

static void test_obsolete_tuyaos_commands(void)
{
    /* TuyaOS-era commands answer "not used with TuyaLink" in every build/state */
    const char *old[] = {"!auth", "!pid", "!reset-tuya"};
    for (int dev = 0; dev < 2; dev++) {
        for (int prov = 0; prov < 2; prov++) {
            clip_ctx_t c = {.dev = dev, .tylink_provisioned = prov, .wifi_provisioned = prov, .prov_window = !prov};
            for (size_t i = 0; i < sizeof(old) / sizeof(old[0]); i++)
                TEST_ASSERT_EQUAL(CLIP_OBSOLETE, clip_check(old[i], &c));
        }
    }
}

static void test_release_first_provisioning_per_item(void)
{
    clip_ctx_t c = {.dev = false, .tylink_provisioned = false, .wifi_provisioned = false};
    TEST_ASSERT_EQUAL(CLIP_ALLOW, clip_check("!tylink", &c));
    TEST_ASSERT_EQUAL(CLIP_ALLOW, clip_check("!wifi", &c));
    c.tylink_provisioned = true; /* after !tylink: Wi-Fi can still be set once */
    TEST_ASSERT_EQUAL(CLIP_LOCKED, clip_check("!tylink", &c));
    TEST_ASSERT_EQUAL(CLIP_ALLOW, clip_check("!wifi", &c));
    c.wifi_provisioned = true;
    TEST_ASSERT_EQUAL(CLIP_LOCKED, clip_check("!wifi", &c));
}

static void test_dev_cli_allows_everything_known(void)
{
    clip_ctx_t c = {.dev = true, .tylink_provisioned = true, .wifi_provisioned = true};
    const char *all[] = {"!status", "!tylink", "!wifi", "!pair", "!unpair", "!wake", "!cmd", "!key"};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) TEST_ASSERT_EQUAL(CLIP_ALLOW, clip_check(all[i], &c));
    TEST_ASSERT_EQUAL(CLIP_UNKNOWN, clip_check("!x", &c));
}

static void test_wifi_line_parsing(void)
{
    char ssid[CLIP_SSID_MAX + 1], pass[CLIP_WIFI_PASS_MAX + 1];
    TEST_ASSERT_EQUAL(0, clip_parse_wifi("!wifi MyNet secretpw", ssid, pass));
    TEST_ASSERT_EQUAL_STRING("MyNet", ssid);
    TEST_ASSERT_EQUAL_STRING("secretpw", pass);
    /* the password is the rest of the line: inner and trailing spaces kept */
    TEST_ASSERT_EQUAL(0, clip_parse_wifi("!wifi  MyNet   my pass  phrase ", ssid, pass));
    TEST_ASSERT_EQUAL_STRING("MyNet", ssid);
    TEST_ASSERT_EQUAL_STRING("my pass  phrase ", pass);
    TEST_ASSERT_EQUAL(0, clip_parse_wifi("!wifi\tNet\tpa ss wo rd", ssid, pass));
    TEST_ASSERT_EQUAL_STRING("pa ss wo rd", pass);
    /* usage errors (the password is mandatory: no silent open network): outputs zeroed */
    const char *bad[] = {"!wifi", "!wifi   ", "!wifi OpenNet", "!wifi OpenNet   ", "!wifix a 12345678", "!wif a 12345678", "!wifi net short",
                         "!wifi 0123456789012345678901234567890123 12345678",
                         "!wifi net 01234567890123456789012345678901234567890123456789012345678901234",
                         "!wifi net pass\x01word", NULL};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_EQUAL(-1, clip_parse_wifi(bad[i], ssid, pass));
        TEST_ASSERT_EQUAL_STRING("", ssid);
        TEST_ASSERT_EQUAL_STRING("", pass);
    }
    /* 32-byte SSID and 64-char password (hex PSK) are the limits */
    TEST_ASSERT_EQUAL(0, clip_parse_wifi("!wifi 01234567890123456789012345678901 "
                                         "0123456789012345678901234567890123456789012345678901234567890123",
                                         ssid, pass));
    TEST_ASSERT_EQUAL(32, strlen(ssid));
    TEST_ASSERT_EQUAL(64, strlen(pass));
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
    lr_set_secret(LR_SLOT_TYLINK_SECRET, "Zx9Kq2Lm8Np4Rs6Tv1Wy3Ab5Cd7Ef0Gh");
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
    lr_set_secret(LR_SLOT_TYLINK_SECRET, "");
    TEST_ASSERT_FALSE(lr_sensitive("Zx9Kq2Lm8Np4Rs6Tv1Wy3Ab5Cd7Ef0Gh"));
    lr_set_secret(LR_SLOT_WIFI_PASS, "my wifi pass phrase");
    lr_set_secret(LR_SLOT_MQTT_PASS, "a7800dea634b3f52f07cb0b8459eed1c2d00f0870743284e6eb687f4a394ee1c");
    TEST_ASSERT_TRUE(lr_sensitive("wifi: connecting with my wifi pass phrase\n"));
    TEST_ASSERT_TRUE(lr_sensitive("mqtt: pw=a7800dea634b3f52f07cb0b8459eed1c2d00f0870743284e6eb687f4a394ee1c"));
    TEST_ASSERT_FALSE(lr_sensitive("tylink: mqtt connected to m1.tuyaeu.com\n"));
    lr_set_secret(LR_SLOT_WIFI_PASS, NULL);
    lr_set_secret(LR_SLOT_MQTT_PASS, NULL);
    lr_set_secret(99, "ignored-slot");
}

void run_security_tests(void)
{
    RUN_TEST(test_release_cli_only_safe_commands);
    RUN_TEST(test_obsolete_tuyaos_commands);
    RUN_TEST(test_release_first_provisioning_per_item);
    RUN_TEST(test_dev_cli_allows_everything_known);
    RUN_TEST(test_wifi_line_parsing);
    RUN_TEST(test_provisioning_window);
    RUN_TEST(test_log_redaction);
}
