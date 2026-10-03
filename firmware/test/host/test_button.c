#include "button_fsm.h"
#include "unity.h"

static btn_fsm_t B;
static uint32_t T;
static btn_ev_t evs[16];
static int nev;

static void hold(bool pressed, uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 20) {
        T += 20;
        btn_ev_t e = btn_update(&B, pressed, T);
        if (e != BTN_NONE && nev < 16) evs[nev++] = e;
    }
}

static void start(void)
{
    T = 0;
    nev = 0;
    btn_init(&B, T);
    hold(false, 200); /* arm */
}

static void test_short_and_double(void)
{
    start();
    hold(true, 100);
    hold(false, 600);
    TEST_ASSERT_EQUAL(1, nev);
    TEST_ASSERT_EQUAL(BTN_SHORT, evs[0]);
    start();
    hold(true, 100);
    hold(false, 150);
    hold(true, 100);
    hold(false, 600);
    TEST_ASSERT_EQUAL(1, nev);
    TEST_ASSERT_EQUAL(BTN_DOUBLE, evs[0]);
    /* two clicks too far apart = two shorts */
    start();
    hold(true, 100);
    hold(false, 700);
    hold(true, 100);
    hold(false, 700);
    TEST_ASSERT_EQUAL(2, nev);
    TEST_ASSERT_EQUAL(BTN_SHORT, evs[1]);
}

static void test_bounce_is_filtered(void)
{
    start();
    for (int i = 0; i < 5; i++) { /* 20 ms chatter */
        hold(true, 20);
        hold(false, 20);
    }
    hold(false, 600);
    TEST_ASSERT_EQUAL(0, nev);
}

static void test_long_presses(void)
{
    start();
    hold(true, 3500);
    TEST_ASSERT_EQUAL(1, nev);
    TEST_ASSERT_EQUAL(BTN_HOLD_3S, evs[0]);
    hold(false, 100);
    TEST_ASSERT_EQUAL(BTN_LONG3, evs[1]);
    start();
    hold(true, 6000); /* 5..10 s: provisioning window */
    hold(false, 100);
    TEST_ASSERT_EQUAL(3, nev);
    TEST_ASSERT_EQUAL(BTN_HOLD_3S, evs[0]);
    TEST_ASSERT_EQUAL(BTN_HOLD_5S, evs[1]);
    TEST_ASSERT_EQUAL(BTN_LONG5, evs[2]);
    start();
    hold(true, 4900); /* just under 5 s: still pairing */
    hold(false, 100);
    TEST_ASSERT_EQUAL(BTN_LONG3, evs[nev - 1]);
    start();
    hold(true, 12000);
    hold(false, 100);
    TEST_ASSERT_EQUAL(4, nev);
    TEST_ASSERT_EQUAL(BTN_HOLD_10S, evs[2]);
    TEST_ASSERT_EQUAL(BTN_LONG10, evs[3]);
    start();
    hold(true, 21000);
    hold(false, 100);
    TEST_ASSERT_EQUAL(4, nev);
    TEST_ASSERT_EQUAL(BTN_HOLD_20S, evs[3]); /* no action on release: HAL handles download mode */
    start();
    hold(true, 2000); /* 1..3 s: ignored */
    hold(false, 600);
    TEST_ASSERT_EQUAL(0, nev);
}

static void test_stuck_low_at_boot_never_arms(void)
{
    T = 0;
    nev = 0;
    btn_init(&B, T);
    hold(true, 30000);
    TEST_ASSERT_EQUAL(0, nev);
    hold(false, 100); /* first release only arms */
    hold(false, 600);
    TEST_ASSERT_EQUAL(0, nev);
    hold(true, 100);
    hold(false, 600);
    TEST_ASSERT_EQUAL(1, nev);
}

void run_button_tests(void)
{
    RUN_TEST(test_short_and_double);
    RUN_TEST(test_bounce_is_filtered);
    RUN_TEST(test_long_presses);
    RUN_TEST(test_stuck_low_at_boot_never_arms);
}
