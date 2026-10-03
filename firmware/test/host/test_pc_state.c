/* Transition table of the PC state machine (US-0015). */
#include "pc_state.h"
#include "unity.h"

static pcs_sm_t S;
static uint32_t T;

static pcs_inputs_t in(bool mounted, bool suspended, bool agent, bool wake)
{
    pcs_inputs_t i = {mounted, suspended, agent, wake, false};
    return i;
}

/* run the inputs for `ms` in 100 ms ticks; returns final state */
static pcs_t run(pcs_inputs_t i, uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 100) {
        T += 100;
        pcs_update(&S, &i, T);
    }
    return S.state;
}

static void start(void)
{
    T = 0;
    pcs_init(&S, T);
}

static void test_initial_unknown_then_settles(void)
{
    start();
    TEST_ASSERT_EQUAL(PCS_UNKNOWN, S.state);
    TEST_ASSERT_EQUAL(PCS_UNKNOWN, run(in(false, false, false, false), 2900));
    TEST_ASSERT_EQUAL(PCS_OFF, run(in(false, false, false, false), 200));
    start();
    TEST_ASSERT_EQUAL(PCS_SLEEP, run(in(true, true, false, false), 3200));
    start();
    /* bus already up when the dongle boots: booting until the grace ends */
    TEST_ASSERT_EQUAL(PCS_BOOTING, run(in(true, false, false, false), 3200));
    TEST_ASSERT_EQUAL(PCS_ON_NO_AGENT, run(in(true, false, false, false), PCS_BOOT_GRACE_MS));
    start();
    TEST_ASSERT_EQUAL(PCS_ON, run(in(true, false, true, false), 100)); /* agent: immediate */
}

static void test_boot_sequence(void)
{
    start();
    run(in(false, false, false, false), 5000);
    TEST_ASSERT_EQUAL(PCS_OFF, S.state);
    /* BIOS enumerates, OS re-enumerates (short umount), agent connects */
    TEST_ASSERT_EQUAL(PCS_BOOTING, run(in(true, false, false, false), 100));
    TEST_ASSERT_EQUAL(PCS_BOOTING, run(in(false, false, false, false), 1500)); /* < 3 s hold: no OFF */
    TEST_ASSERT_EQUAL(PCS_BOOTING, run(in(true, false, false, false), 20000));
    TEST_ASSERT_EQUAL(PCS_ON, run(in(true, false, true, false), 100));
}

static void test_boot_without_agent(void)
{
    start();
    run(in(false, false, false, false), 5000);
    run(in(true, false, false, false), PCS_BOOT_GRACE_MS - 1000);
    TEST_ASSERT_EQUAL(PCS_BOOTING, S.state);
    TEST_ASSERT_EQUAL(PCS_ON_NO_AGENT, run(in(true, false, false, false), 3500));
    TEST_ASSERT_FALSE(S.agent_lost);
}

static void test_agent_lost(void)
{
    start();
    run(in(true, false, true, false), 4000);
    TEST_ASSERT_EQUAL(PCS_ON, S.state);
    TEST_ASSERT_EQUAL(PCS_ON, run(in(true, false, false, false), 1900)); /* hold 2 s */
    TEST_ASSERT_EQUAL(PCS_ON_NO_AGENT, run(in(true, false, false, false), 200));
    TEST_ASSERT_TRUE(S.agent_lost);
    TEST_ASSERT_EQUAL(PCS_ON, run(in(true, false, true, false), 100));
    TEST_ASSERT_FALSE(S.agent_lost);
}

static void test_suspend_resume(void)
{
    start();
    run(in(true, false, true, false), 4000);
    /* OS suspends: agent stops, bus suspends */
    run(in(true, true, false, false), 2900);
    TEST_ASSERT_EQUAL(PCS_ON, S.state);
    TEST_ASSERT_EQUAL(PCS_SLEEP, run(in(true, true, false, false), 200));
    TEST_ASSERT_FALSE(S.agent_lost); /* expected loss */
    /* resume: booting until the agent reconnects */
    TEST_ASSERT_EQUAL(PCS_BOOTING, run(in(true, false, false, false), 100));
    TEST_ASSERT_EQUAL(PCS_ON, run(in(true, false, true, false), 100));
}

static void test_shutdown(void)
{
    start();
    run(in(true, false, true, false), 4000);
    run(in(false, false, false, false), 2900);
    TEST_ASSERT_EQUAL(PCS_ON, S.state);
    TEST_ASSERT_EQUAL(PCS_OFF, run(in(false, false, false, false), 200));
    TEST_ASSERT_FALSE(S.agent_lost);
}

static void test_shutdown_with_always_on_usb(void)
{
    /* S5 with powered USB: the bus only suspends; an acked shutdown makes it OFF */
    start();
    run(in(true, false, true, false), 4000);
    pcs_inputs_t i = in(true, true, false, false);
    i.shutdown_expected = true;
    TEST_ASSERT_EQUAL(PCS_OFF, run(i, 3200));
    /* without the hint a suspended bus is SLEEP */
    start();
    run(in(true, false, true, false), 4000);
    TEST_ASSERT_EQUAL(PCS_SLEEP, run(in(true, true, false, false), 3200));
}

static void test_wake_in_progress(void)
{
    start();
    run(in(false, false, false, false), 5000);
    TEST_ASSERT_EQUAL(PCS_BOOTING, run(in(false, false, false, true), 100));
    TEST_ASSERT_EQUAL(PCS_BOOTING, run(in(true, false, false, true), 1000));
    TEST_ASSERT_EQUAL(PCS_BOOTING, run(in(true, false, false, false), 1000));
    /* wake gave up and bus still down -> OFF after hold */
    start();
    run(in(false, false, false, false), 5000);
    run(in(false, false, false, true), 1000);
    TEST_ASSERT_EQUAL(PCS_OFF, run(in(false, false, false, false), 3100));
    /* sleeping PC woken */
    start();
    run(in(true, true, false, false), 5000);
    TEST_ASSERT_EQUAL(PCS_SLEEP, S.state);
    TEST_ASSERT_EQUAL(PCS_BOOTING, run(in(true, true, false, true), 100));
}

static void test_no_flapping(void)
{
    start();
    run(in(true, false, true, false), 4000);
    uint32_t c = S.changes;
    /* noisy suspend glitches shorter than the hold never leave ON... */
    for (int i = 0; i < 20; i++) {
        run(in(true, true, true, false), 500);
        run(in(true, false, true, false), 500);
    }
    TEST_ASSERT_EQUAL(c, S.changes);
    /* ...nor do short unmounts while the agent is gone */
    run(in(true, false, false, false), 2500);
    c = S.changes;
    for (int i = 0; i < 10; i++) {
        run(in(false, false, false, false), 1000);
        run(in(true, false, false, false), 500);
    }
    TEST_ASSERT_TRUE(S.changes - c <= 1); /* at most ON_NO_AGENT -> BOOTING once */
}

static void test_names(void)
{
    TEST_ASSERT_EQUAL_STRING("off", pcs_name(PCS_OFF));
    TEST_ASSERT_EQUAL_STRING("on_no_agent", pcs_name(PCS_ON_NO_AGENT));
    TEST_ASSERT_EQUAL_STRING("unknown", pcs_name(PCS_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("?", pcs_name((pcs_t)42));
}

void run_pc_state_tests(void)
{
    RUN_TEST(test_initial_unknown_then_settles);
    RUN_TEST(test_boot_sequence);
    RUN_TEST(test_boot_without_agent);
    RUN_TEST(test_agent_lost);
    RUN_TEST(test_suspend_resume);
    RUN_TEST(test_shutdown);
    RUN_TEST(test_shutdown_with_always_on_usb);
    RUN_TEST(test_wake_in_progress);
    RUN_TEST(test_no_flapping);
    RUN_TEST(test_names);
}
