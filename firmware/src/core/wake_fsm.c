#include "wake_fsm.h"

#include <string.h>

#define ELAPSED(now, t) ((int32_t)((uint32_t)(now) - (uint32_t)(t)))

static const char *const k_m[WM__COUNT] = {"hid", "wol", "hid_then_wol"};

const char *wake_method_name(wake_method_t m) { return (unsigned)m < WM__COUNT ? k_m[m] : "?"; }

int wake_method_parse(const char *s)
{
    for (int i = 0; i < WM__COUNT; i++)
        if (s && !strcmp(s, k_m[i])) return i;
    return -1;
}

void wol_build(const uint8_t mac[6], uint8_t out[WOL_PKT_LEN])
{
    memset(out, 0xFF, 6);
    for (int i = 0; i < 16; i++) memcpy(out + 6 + 6 * i, mac, 6);
}

void wake_init(wake_t *w, const wake_cbs_t *cb)
{
    memset(w, 0, sizeof(*w));
    if (cb) w->cb = *cb;
}

static bool up(const wake_usb_t *u) { return u->mounted && !u->suspended; }

static void hid_step(wake_t *w, const wake_usb_t *u, uint32_t now)
{
    w->last_hid_ms = now;
    if (u->mounted && u->suspended && u->rwu_armed) {
        w->force_path = false;
        if (w->cb.hid_remote_wakeup && w->cb.hid_remote_wakeup(w->cb.ctx) == 0) return;
    }
    /* not mounted, not armed, or the standard path failed: forced resume signalling */
    w->force_path = true;
    if (w->cb.hid_force_resume) w->cb.hid_force_resume(w->cb.ctx);
}

const char *wake_plan(wake_method_t m, const wake_usb_t *u)
{
    if (up(u)) return "PC on (bus mounted, not suspended): wake would be ignored";
    if (m == WM_WOL) return "WOL magic packet to the stored MACs";
    const char *hid = (u->mounted && u->suspended && u->rwu_armed)
                          ? "HID remote wakeup (bus suspended, wakeup armed)"
                          : "HID forced resume signalling (bus not mounted or wakeup not armed; best effort)";
    return m == WM_HID_THEN_WOL ? (u->mounted && u->suspended && u->rwu_armed
                                        ? "HID remote wakeup, then WOL after 20 s without mount"
                                        : "HID forced resume, then WOL after 20 s without mount")
                                : hid;
}

wake_rc_t wake_request(wake_t *w, wake_method_t m, const wake_usb_t *u, uint32_t now_ms)
{
    if (up(u)) return WAKE_IGNORED_ON;
    if (w->active) return WAKE_BUSY;
    if ((unsigned)m >= WM__COUNT) m = WM_HID;
    w->method = m;
    w->wol_sent = false;
    w->hid_retries = 0;
    w->force_path = false;
    if (m == WM_WOL) {
        int n = w->cb.wol_send ? w->cb.wol_send(w->cb.ctx) : 0;
        if (n <= 0) {
            w->failures++;
            w->fault_failed = true;
            if (w->cb.done) w->cb.done(w->cb.ctx, false);
            return WAKE_NO_TARGET;
        }
        w->wol_sent = true;
    } else {
        hid_step(w, u, now_ms);
    }
    w->active = true;
    w->start_ms = now_ms;
    w->attempts++;
    if (w->cb.sent) w->cb.sent(w->cb.ctx);
    return WAKE_STARTED;
}

void wake_tick(wake_t *w, const wake_usb_t *u, uint32_t now_ms)
{
    if (!w->active) return;
    if (up(u)) {
        w->active = false;
        w->fault_failed = false;
        w->successes++;
        if (w->cb.done) w->cb.done(w->cb.ctx, true);
        return;
    }
    int32_t el = ELAPSED(now_ms, w->start_ms);
    if (w->method != WM_WOL && w->force_path && w->hid_retries < WAKE_HID_RETRIES &&
        ELAPSED(now_ms, w->last_hid_ms) >= WAKE_HID_RETRY_MS) {
        w->hid_retries++;
        hid_step(w, u, now_ms);
    }
    if (w->method == WM_HID_THEN_WOL && !w->wol_sent && el >= WAKE_WOL_AFTER_MS) {
        w->wol_sent = true;
        if (w->cb.wol_send) w->cb.wol_send(w->cb.ctx);
    }
    if (el >= WAKE_TIMEOUT_MS) {
        w->active = false;
        w->fault_failed = true;
        w->failures++;
        if (w->cb.done) w->cb.done(w->cb.ctx, false);
    }
}
