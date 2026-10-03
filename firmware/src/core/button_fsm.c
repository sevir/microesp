#include "button_fsm.h"

#include <string.h>

#define ELAPSED(now, t) ((int32_t)((uint32_t)(now) - (uint32_t)(t)))

const char *btn_ev_name(btn_ev_t e)
{
    static const char *const n[] = {"none", "short", "double", "long3", "long10", "hold3", "hold10", "hold20",
                                     "long5", "hold5"};
    return (unsigned)e < sizeof(n) / sizeof(n[0]) ? n[e] : "?";
}

void btn_init(btn_fsm_t *b, uint32_t now_ms)
{
    memset(b, 0, sizeof(*b));
    b->raw_since = now_ms;
}

btn_ev_t btn_update(btn_fsm_t *b, bool pressed, uint32_t now_ms)
{
    if (pressed != b->raw) {
        b->raw = pressed;
        b->raw_since = now_ms;
    }
    bool changed = false;
    if (b->raw != b->stable && ELAPSED(now_ms, b->raw_since) >= BTN_DEBOUNCE_MS) {
        b->stable = b->raw;
        changed = true;
    }
    if (!b->armed) {
        /* arm only after a stable release */
        if (!b->stable && ELAPSED(now_ms, b->raw_since) >= BTN_DEBOUNCE_MS) b->armed = true;
        return BTN_NONE;
    }
    if (changed && b->stable) { /* press */
        b->down_ms = now_ms;
        b->hint_level = 0;
        b->second = b->click_pending && ELAPSED(now_ms, b->click_up_ms) <= BTN_DOUBLE_GAP_MS;
        b->click_pending = false;
        return BTN_NONE;
    }
    if (changed && !b->stable) { /* release */
        int32_t d = ELAPSED(now_ms, b->down_ms);
        b->hint_level = 0;
        if (d < BTN_CLICK_MAX_MS) {
            if (b->second) {
                b->second = false;
                return BTN_DOUBLE;
            }
            b->click_pending = true;
            b->click_up_ms = now_ms;
            return BTN_NONE;
        }
        b->second = false;
        if (d >= BTN_LONG3_MS && d < BTN_LONG5_MS) return BTN_LONG3;
        if (d >= BTN_LONG5_MS && d < BTN_LONG10_MS) return BTN_LONG5;
        if (d >= BTN_LONG10_MS && d < BTN_DFU_MS) return BTN_LONG10;
        return BTN_NONE;
    }
    if (b->stable) { /* held: hints */
        int32_t d = ELAPSED(now_ms, b->down_ms);
        if (b->hint_level < 1 && d >= BTN_LONG3_MS) {
            b->hint_level = 1;
            return BTN_HOLD_3S;
        }
        if (b->hint_level < 2 && d >= BTN_LONG5_MS) {
            b->hint_level = 2;
            return BTN_HOLD_5S;
        }
        if (b->hint_level < 3 && d >= BTN_LONG10_MS) {
            b->hint_level = 3;
            return BTN_HOLD_10S;
        }
        if (b->hint_level < 4 && d >= BTN_DFU_MS) {
            b->hint_level = 4;
            return BTN_HOLD_20S;
        }
        return BTN_NONE;
    }
    /* released and idle: flush a pending single click */
    if (b->click_pending && ELAPSED(now_ms, b->click_up_ms) > BTN_DOUBLE_GAP_MS) {
        b->click_pending = false;
        return BTN_SHORT;
    }
    return BTN_NONE;
}
