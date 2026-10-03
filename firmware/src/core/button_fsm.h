/*
 * MicroESP — BOOT button gesture detector (US-0012), pure C and host-tested.
 *
 *   click (< 1 s), single            -> BTN_SHORT  (cancel countdown / next screen)
 *   two clicks within 400 ms          -> BTN_DOUBLE (wake PC)
 *   hold 3..5 s, then release         -> BTN_LONG3  (agent pairing mode)
 *   hold 5..10 s, then release        -> BTN_LONG5  (provisioning window: TuyaLink/Wi-Fi
 *                                        credentials via the CDC CLI for 120 s)
 *   hold 10..20 s, then release       -> BTN_LONG10 (unused since TuyaLink)
 *   hold >= 20 s                      -> nothing here: the HAL supervisor enters ROM
 *                                        download mode (emergency recovery)
 * Holds of 1..3 s are ignored. Nothing is reported until the button has been seen
 * released once (guards against a stuck-low GPIO0 at boot). 40 ms debounce.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BTN_DEBOUNCE_MS   40
#define BTN_CLICK_MAX_MS  1000
#define BTN_DOUBLE_GAP_MS 400
#define BTN_LONG3_MS      3000
#define BTN_LONG5_MS      5000
#define BTN_LONG10_MS     10000
#define BTN_DFU_MS        20000

typedef enum {
    BTN_NONE = 0,
    BTN_SHORT,
    BTN_DOUBLE,
    BTN_LONG3,
    BTN_LONG10,
    BTN_HOLD_3S,  /* hint while held: 3 s reached */
    BTN_HOLD_10S, /* hint while held: 10 s reached */
    BTN_HOLD_20S, /* hint while held: download-mode threshold reached */
    BTN_LONG5,    /* hold 5..10 s released: provisioning window */
    BTN_HOLD_5S,  /* hint while held: 5 s reached */
} btn_ev_t;

typedef struct {
    bool armed;
    bool raw, stable;
    uint32_t raw_since;
    uint32_t down_ms;
    int hint_level;
    bool click_pending;
    bool second; /* current press is the 2nd click of a double */
    uint32_t click_up_ms;
} btn_fsm_t;

void btn_init(btn_fsm_t *b, uint32_t now_ms);
/* pressed = electrical state (true = pressed). Returns at most one event per call. */
btn_ev_t btn_update(btn_fsm_t *b, bool pressed, uint32_t now_ms);
const char *btn_ev_name(btn_ev_t e);

#ifdef __cplusplus
}
#endif
