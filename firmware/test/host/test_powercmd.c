#include <stdio.h>
#include <string.h>

#include "powercmd.h"
#include "unity.h"

static pwr_t P;
static char notices[8][32];
static int nnotice, nreset, ncmd;
static last_result_t last;
static int nresult;
static uint32_t next_id;

static bool c_notice(void *ctx, const char *a, int in)
{
    snprintf(notices[nnotice++ & 7], 32, "%s:%d", a, in);
    return true;
}
static uint32_t c_cmd(void *ctx, const char *a)
{
    ncmd++;
    return next_id;
}
static void c_result(void *ctx, pwr_action_t a, last_result_t r)
{
    last = r;
    nresult++;
}
static void c_reset(void *ctx) { nreset++; }

static void setup(void)
{
    nnotice = nreset = ncmd = nresult = 0;
    last = LR__COUNT;
    next_id = 1;
    pwr_cbs_t cb = {c_notice, c_cmd, c_result, c_reset, NULL};
    pwr_init(&P, &cb);
}

static void test_happy_path(void)
{
    setup();
    TEST_ASSERT_TRUE(pwr_request(&P, PWR_REBOOT, 10, true, 1000));
    TEST_ASSERT_EQUAL(PWR_COUNTDOWN, P.st);
    TEST_ASSERT_EQUAL_STRING("reboot:10", notices[0]);
    TEST_ASSERT_EQUAL(10, pwr_remaining_s(&P, 1000));
    TEST_ASSERT_EQUAL(5, pwr_remaining_s(&P, 6000));
    TEST_ASSERT_EQUAL(6, pwr_remaining_s(&P, 5500));
    pwr_tick(&P, true, 10999);
    TEST_ASSERT_EQUAL(0, ncmd);
    pwr_tick(&P, true, 11000);
    TEST_ASSERT_EQUAL(1, ncmd);
    TEST_ASSERT_EQUAL(PWR_WAIT_ACK, P.st);
    pwr_on_ack(&P, 99, true); /* wrong id */
    TEST_ASSERT_EQUAL(0, nresult);
    pwr_on_ack(&P, 1, true);
    TEST_ASSERT_EQUAL(LR_OK, last);
    TEST_ASSERT_EQUAL(1, nreset);
    TEST_ASSERT_EQUAL(PWR_IDLE, P.st);
}

static void test_agent_offline(void)
{
    setup();
    TEST_ASSERT_TRUE(pwr_request(&P, PWR_SHUTDOWN, 10, false, 0));
    TEST_ASSERT_EQUAL(LR_AGENT_OFFLINE, last);
    TEST_ASSERT_EQUAL(1, nreset);
    TEST_ASSERT_EQUAL(0, nnotice);
    /* agent disappears during the countdown */
    setup();
    pwr_request(&P, PWR_SHUTDOWN, 3, true, 0);
    pwr_tick(&P, false, 3000);
    TEST_ASSERT_EQUAL(LR_AGENT_OFFLINE, last);
    TEST_ASSERT_EQUAL(0, ncmd);
    /* link refuses to send (no session) */
    setup();
    next_id = 0;
    pwr_request(&P, PWR_SHUTDOWN, 0, true, 0);
    TEST_ASSERT_EQUAL(LR_AGENT_OFFLINE, last);
}

static void test_cancel(void)
{
    setup();
    pwr_request(&P, PWR_SHUTDOWN, 10, true, 0);
    TEST_ASSERT_TRUE(pwr_cancel(&P));
    TEST_ASSERT_EQUAL(LR_CANCELLED, last);
    TEST_ASSERT_EQUAL_STRING("cancel:0", notices[1]);
    TEST_ASSERT_EQUAL(1, nreset);
    pwr_tick(&P, true, 20000);
    TEST_ASSERT_EQUAL(0, ncmd);
    TEST_ASSERT_FALSE(pwr_cancel(&P)); /* nothing to cancel */
    /* too late once the cmd was sent */
    setup();
    pwr_request(&P, PWR_SHUTDOWN, 0, true, 0);
    TEST_ASSERT_FALSE(pwr_cancel(&P));
    TEST_ASSERT_EQUAL(PWR_WAIT_ACK, P.st);
}

static void test_rejected_and_timeout(void)
{
    setup();
    pwr_request(&P, PWR_REBOOT, 0, true, 0);
    pwr_on_ack(&P, 1, false);
    TEST_ASSERT_EQUAL(LR_CMD_REJECTED, last);
    setup();
    pwr_request(&P, PWR_REBOOT, 0, true, 0);
    pwr_on_ack_timeout(&P, 1);
    TEST_ASSERT_EQUAL(LR_CMD_REJECTED, last);
    TEST_ASSERT_EQUAL(1, nreset);
}

static void test_busy_and_bounds(void)
{
    setup();
    TEST_ASSERT_TRUE(pwr_request(&P, PWR_REBOOT, 100, true, 0));
    TEST_ASSERT_EQUAL(60, pwr_remaining_s(&P, 0)); /* clamped to 60 s */
    TEST_ASSERT_FALSE(pwr_request(&P, PWR_SHUTDOWN, 5, true, 0));
    TEST_ASSERT_FALSE(pwr_request(&P, PWR_NONE, 5, true, 0));
    TEST_ASSERT_EQUAL_STRING("cmd_rejected", lr_name(LR_CMD_REJECTED));
    TEST_ASSERT_EQUAL_STRING("shutdown", pwr_action_name(PWR_SHUTDOWN));
}

void run_powercmd_tests(void)
{
    RUN_TEST(test_happy_path);
    RUN_TEST(test_agent_offline);
    RUN_TEST(test_cancel);
    RUN_TEST(test_rejected_and_timeout);
    RUN_TEST(test_busy_and_bounds);
}
