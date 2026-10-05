/* User script run flow (core/scriptcmd.c): results for DP 113 and independence from
 * the shutdown/reboot countdown (core/powercmd.c). */
#include <string.h>

#include "powercmd.h"
#include "scriptcmd.h"
#include "unity.h"

static scr_t S;
static uint32_t next_id, sent;
static char sent_action[32];
static int nres;
static last_result_t res;
static char res_id[16];

static uint32_t cb_cmd(void *c, const char *action)
{
    sent++;
    strncpy(sent_action, action, sizeof(sent_action) - 1);
    return next_id;
}

static void cb_result(void *c, const char *id, last_result_t r)
{
    nres++;
    res = r;
    strncpy(res_id, id, sizeof(res_id) - 1);
}

static void reset(uint32_t id)
{
    scr_cbs_t cb = {cb_cmd, cb_result, NULL};
    scr_init(&S, &cb);
    next_id = id;
    sent = 0;
    nres = 0;
    memset(sent_action, 0, sizeof(sent_action));
    memset(res_id, 0, sizeof(res_id));
}

static void test_script_ok_and_rejected_ack(void)
{
    reset(3);
    TEST_ASSERT_TRUE(scr_request(&S, "backup", true, true));
    TEST_ASSERT_EQUAL_STRING("script:backup", sent_action);
    TEST_ASSERT_EQUAL(0, nres);
    TEST_ASSERT_TRUE(S.waiting);
    scr_on_ack(&S, 2, true); /* another cmd's ack (e.g. the shutdown): ignored */
    TEST_ASSERT_EQUAL(0, nres);
    scr_on_ack(&S, 3, true);
    TEST_ASSERT_EQUAL(1, nres);
    TEST_ASSERT_EQUAL(LR_OK, res);
    TEST_ASSERT_EQUAL_STRING("backup", res_id);
    TEST_ASSERT_FALSE(S.waiting);
    scr_on_ack(&S, 3, true); /* late duplicate: ignored */
    TEST_ASSERT_EQUAL(1, nres);

    next_id = 4;
    TEST_ASSERT_TRUE(scr_request(&S, "backup", true, true));
    scr_on_ack(&S, 4, false); /* unknown_action / exec_failed / bad_sig */
    TEST_ASSERT_EQUAL(LR_CMD_REJECTED, res);
    TEST_ASSERT_EQUAL(1, S.ok);
    TEST_ASSERT_EQUAL(1, S.rejected);
}

static void test_script_immediate_results(void)
{
    reset(1);
    TEST_ASSERT_FALSE(scr_request(&S, "backup", false, true)); /* no agent session */
    TEST_ASSERT_EQUAL(LR_AGENT_OFFLINE, res);
    TEST_ASSERT_EQUAL(0, sent);
    TEST_ASSERT_FALSE(scr_request(&S, "nope", true, false)); /* not in the list */
    TEST_ASSERT_EQUAL(LR_CMD_REJECTED, res);
    TEST_ASSERT_EQUAL(0, sent);
    TEST_ASSERT_FALSE(scr_request(&S, NULL, true, true));
    TEST_ASSERT_EQUAL(LR_CMD_REJECTED, res);
    next_id = 0; /* link refused to send */
    TEST_ASSERT_FALSE(scr_request(&S, "backup", true, true));
    TEST_ASSERT_EQUAL(LR_CMD_REJECTED, res);
    TEST_ASSERT_EQUAL(4, nres);
    TEST_ASSERT_FALSE(S.waiting);
}

static void test_script_busy_and_timeout(void)
{
    reset(7);
    TEST_ASSERT_TRUE(scr_request(&S, "backup", true, true));
    /* a second run while the first awaits its ack: rejected, the first keeps waiting */
    TEST_ASSERT_FALSE(scr_request(&S, "docker-up", true, true));
    TEST_ASSERT_EQUAL(1, nres);
    TEST_ASSERT_EQUAL(LR_CMD_REJECTED, res);
    TEST_ASSERT_EQUAL_STRING("docker-up", res_id);
    TEST_ASSERT_EQUAL(1, sent);
    TEST_ASSERT_TRUE(S.waiting);
    scr_on_ack_timeout(&S, 6); /* someone else's */
    TEST_ASSERT_EQUAL(1, nres);
    scr_on_ack_timeout(&S, 7); /* 10 s without ack, or session dropped */
    TEST_ASSERT_EQUAL(2, nres);
    TEST_ASSERT_EQUAL(LR_CMD_REJECTED, res);
    TEST_ASSERT_EQUAL_STRING("backup", res_id);
    TEST_ASSERT_FALSE(S.waiting);
}

/* A running shutdown countdown is not touched by a script run: different state
 * machines, and the link keeps one pending cmd per class. */
static int p_results;
static last_result_t p_last;
static uint32_t p_cmd(void *c, const char *a) { return 9; }
static void p_result(void *c, pwr_action_t a, last_result_t r)
{
    p_results++;
    p_last = r;
}

static void test_script_does_not_disturb_countdown(void)
{
    pwr_t P;
    pwr_cbs_t pcb = {NULL, p_cmd, p_result, NULL, NULL};
    pwr_init(&P, &pcb);
    p_results = 0;
    TEST_ASSERT_TRUE(pwr_request(&P, PWR_SHUTDOWN, 10, true, 0));
    reset(8);
    TEST_ASSERT_TRUE(scr_request(&S, "backup", true, true));
    scr_on_ack(&S, 8, true);
    pwr_on_ack(&P, 8, true); /* the script's ack never resolves the power flow */
    TEST_ASSERT_EQUAL(PWR_COUNTDOWN, P.st);
    TEST_ASSERT_EQUAL(10, pwr_remaining_s(&P, 0));
    pwr_tick(&P, true, 10000);
    TEST_ASSERT_EQUAL(PWR_WAIT_ACK, P.st);
    scr_on_ack_timeout(&S, 9); /* the power cmd's events never reach the script flow */
    TEST_ASSERT_EQUAL(LR_OK, res);
    pwr_on_ack(&P, 9, true);
    TEST_ASSERT_EQUAL(1, p_results);
    TEST_ASSERT_EQUAL(LR_OK, p_last);
}

void run_scripts_tests(void)
{
    RUN_TEST(test_script_ok_and_rejected_ack);
    RUN_TEST(test_script_immediate_results);
    RUN_TEST(test_script_busy_and_timeout);
    RUN_TEST(test_script_does_not_disturb_countdown);
}
