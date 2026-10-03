#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void run_crypto_tests(void);
void run_link_tests(void);
void run_fuzz_tests(void);
void run_pc_state_tests(void);
void run_powercmd_tests(void);
void run_wake_tests(void);
void run_button_tests(void);
void run_dp_model_tests(void);
void run_security_tests(void);

int main(void)
{
    UNITY_BEGIN();
    run_crypto_tests();
    run_link_tests();
    run_fuzz_tests();
    run_pc_state_tests();
    run_powercmd_tests();
    run_wake_tests();
    run_button_tests();
    run_dp_model_tests();
    run_security_tests();
    return UNITY_END();
}
