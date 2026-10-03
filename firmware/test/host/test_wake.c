#include <string.h>

#include "unity.h"
#include "wake_fsm.h"

static wake_t W;
static int n_rwu, n_force, n_wol, n_sent, n_ok, n_fail, wol_targets, rwu_rc;

static int c_rwu(void *c)
{
    n_rwu++;
    return rwu_rc;
}
static int c_force(void *c)
{
    n_force++;
    return 0;
}
static int c_wol(void *c)
{
    n_wol++;
    return wol_targets;
}
static void c_sent(void *c) { n_sent++; }
static void c_done(void *c, bool ok) { ok ? n_ok++ : n_fail++; }

static void setup(int targets)
{
    n_rwu = n_force = n_wol = n_sent = n_ok = n_fail = 0;
    rwu_rc = 0;
    wol_targets = targets;
    wake_cbs_t cb = {c_rwu, c_force, c_wol, c_sent, c_done, NULL};
    wake_init(&W, &cb);
}

static const wake_usb_t ON = {true, false, false};
static const wake_usb_t SUSP_ARMED = {true, true, true};
static const wake_usb_t SUSP_UNARMED = {true, true, false};
static const wake_usb_t OFF = {false, false, false};

static void test_ignored_when_on(void)
{
    setup(1);
    TEST_ASSERT_EQUAL(WAKE_IGNORED_ON, wake_request(&W, WM_HID_THEN_WOL, &ON, 0));
    TEST_ASSERT_EQUAL(0, n_rwu + n_force + n_wol + n_sent);
    TEST_ASSERT_NOT_NULL(strstr(wake_plan(WM_HID, &ON), "ignored"));
}

static void test_hid_from_sleep(void)
{
    setup(1);
    TEST_ASSERT_EQUAL(WAKE_STARTED, wake_request(&W, WM_HID, &SUSP_ARMED, 0));
    TEST_ASSERT_EQUAL(1, n_rwu);
    TEST_ASSERT_EQUAL(0, n_force);
    TEST_ASSERT_EQUAL(1, n_sent);
    TEST_ASSERT_EQUAL(WAKE_BUSY, wake_request(&W, WM_HID, &SUSP_ARMED, 100));
    wake_tick(&W, &SUSP_ARMED, 3000);
    TEST_ASSERT_EQUAL(0, n_force); /* no forced retries on the standard path */
    wake_tick(&W, &ON, 4000);
    TEST_ASSERT_EQUAL(1, n_ok);
    TEST_ASSERT_FALSE(W.active);
    TEST_ASSERT_FALSE(W.fault_failed);
}

static void test_hid_forced_when_not_armed_or_off(void)
{
    setup(1);
    wake_request(&W, WM_HID, &SUSP_UNARMED, 0);
    TEST_ASSERT_EQUAL(0, n_rwu);
    TEST_ASSERT_EQUAL(1, n_force);
    setup(1);
    rwu_rc = -1; /* standard path refused -> fall back to forced resume */
    wake_request(&W, WM_HID, &SUSP_ARMED, 0);
    TEST_ASSERT_EQUAL(1, n_rwu);
    TEST_ASSERT_EQUAL(1, n_force);
    setup(1);
    wake_request(&W, WM_HID, &OFF, 0);
    TEST_ASSERT_EQUAL(1, n_force);
    for (uint32_t t = 100; t <= 10000; t += 100) wake_tick(&W, &OFF, t);
    TEST_ASSERT_EQUAL(1 + WAKE_HID_RETRIES, n_force);
    TEST_ASSERT_EQUAL(0, n_wol); /* pure hid never sends WOL */
}

static void test_hid_then_wol_and_timeout(void)
{
    setup(2);
    wake_request(&W, WM_HID_THEN_WOL, &OFF, 0);
    for (uint32_t t = 100; t < WAKE_WOL_AFTER_MS; t += 100) wake_tick(&W, &OFF, t);
    TEST_ASSERT_EQUAL(0, n_wol);
    wake_tick(&W, &OFF, WAKE_WOL_AFTER_MS);
    TEST_ASSERT_EQUAL(1, n_wol);
    for (uint32_t t = WAKE_WOL_AFTER_MS; t < WAKE_TIMEOUT_MS; t += 1000) wake_tick(&W, &OFF, t);
    TEST_ASSERT_EQUAL(1, n_wol);
    TEST_ASSERT_EQUAL(0, n_fail);
    wake_tick(&W, &OFF, WAKE_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(1, n_fail);
    TEST_ASSERT_TRUE(W.fault_failed);
    /* a later success clears the fault */
    wake_request(&W, WM_HID_THEN_WOL, &OFF, 200000);
    wake_tick(&W, &ON, 205000);
    TEST_ASSERT_EQUAL(1, n_ok);
    TEST_ASSERT_FALSE(W.fault_failed);
}

static void test_wol_only(void)
{
    setup(0);
    TEST_ASSERT_EQUAL(WAKE_NO_TARGET, wake_request(&W, WM_WOL, &OFF, 0));
    TEST_ASSERT_EQUAL(1, n_fail);
    TEST_ASSERT_TRUE(W.fault_failed);
    TEST_ASSERT_FALSE(W.active);
    setup(3);
    TEST_ASSERT_EQUAL(WAKE_STARTED, wake_request(&W, WM_WOL, &OFF, 0));
    TEST_ASSERT_EQUAL(1, n_wol);
    TEST_ASSERT_EQUAL(0, n_force + n_rwu);
    for (uint32_t t = 0; t < 30000; t += 100) wake_tick(&W, &OFF, t);
    TEST_ASSERT_EQUAL(1, n_wol);
}

static void test_magic_packet_and_names(void)
{
    uint8_t mac[6] = {0xfc, 0x9d, 0x05, 0x18, 0xee, 0x32}, p[WOL_PKT_LEN];
    wol_build(mac, p);
    for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_HEX8(0xff, p[i]);
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_HEX8_ARRAY(mac, p + 6 + 6 * i, 6);
    TEST_ASSERT_EQUAL(WM_HID_THEN_WOL, wake_method_parse("hid_then_wol"));
    TEST_ASSERT_EQUAL(-1, wake_method_parse("magic"));
    TEST_ASSERT_EQUAL_STRING("wol", wake_method_name(WM_WOL));
}

void run_wake_tests(void)
{
    RUN_TEST(test_ignored_when_on);
    RUN_TEST(test_hid_from_sleep);
    RUN_TEST(test_hid_forced_when_not_armed_or_off);
    RUN_TEST(test_hid_then_wol_and_timeout);
    RUN_TEST(test_wol_only);
    RUN_TEST(test_magic_packet_and_names);
}
