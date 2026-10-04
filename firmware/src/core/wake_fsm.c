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

/* Agent online, or a new mount/resume edge since the start / the last Alt+P. */
static bool succeeded(const wake_t *w, const wake_usb_t *u)
{
    return u->agent_online || (up(u) && !w->keys_pending && u->up_seq != w->ref_seq);
}

static void send_keys(wake_t *w, const wake_usb_t *u, uint32_t now)
{
    w->last_hid_ms = now;
    w->keys_pending = false;
    w->keys_sent = true;
    w->ref_seq = u->up_seq;
    if (w->cb.hid_keys) w->cb.hid_keys(w->cb.ctx);
}

static void hid_step(wake_t *w, const wake_usb_t *u, uint32_t now)
{
    w->last_hid_ms = now;
    if (up(u)) {
        send_keys(w, u, now);
        return;
    }
    w->keys_pending = true; /* Alt+P once the bus is up again */
    if (u->mounted && u->suspended && u->rwu_armed) {
        if (w->cb.hid_remote_wakeup && w->cb.hid_remote_wakeup(w->cb.ctx) == 0) return;
    }
    /* not mounted, not armed, or the standard path failed: forced resume signalling */
    if (w->cb.hid_force_resume) w->cb.hid_force_resume(w->cb.ctx);
}

const char *wake_plan(wake_method_t m, const wake_usb_t *u)
{
    if (m == WM_WOL) return "WOL magic packet to the stored MACs";
    const char *hid = up(u) ? "HID Alt+P (bus up: Lenovo Smart Power On, or PC already on)"
                      : (u->mounted && u->suspended && u->rwu_armed)
                          ? "HID remote wakeup, then Alt+P when the bus resumes"
                          : "HID forced resume signalling, then Alt+P if the bus comes up (best effort)";
    if (m == WM_HID) return hid;
    return up(u) ? "HID Alt+P + WOL now, WOL again after 20 s without success"
           : (u->mounted && u->suspended && u->rwu_armed)
               ? "HID remote wakeup + Alt+P + WOL now, WOL again after 20 s without success"
               : "HID forced resume + Alt+P + WOL now, WOL again after 20 s without success";
}

wake_rc_t wake_request(wake_t *w, wake_method_t m, const wake_usb_t *u, uint32_t now_ms)
{
    if (w->active) {
        /* a new command while waking: send the HID step again (never ignored) */
        if (w->method != WM_WOL) hid_step(w, u, now_ms);
        return WAKE_RESENT;
    }
    if ((unsigned)m >= WM__COUNT) m = WM_HID;
    w->method = m;
    w->wol_sent = false;
    w->keys_pending = false;
    w->keys_sent = false;
    w->ref_seq = u->up_seq;
    w->hid_retries = 0;
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
        if (m == WM_HID_THEN_WOL && w->cb.wol_send) w->cb.wol_send(w->cb.ctx);
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
    if (w->keys_pending && up(u)) send_keys(w, u, now_ms);
    if (succeeded(w, u)) {
        w->active = false;
        w->keys_pending = false;
        w->fault_failed = false;
        w->successes++;
        if (w->cb.done) w->cb.done(w->cb.ctx, true);
        return;
    }
    int32_t el = ELAPSED(now_ms, w->start_ms);
    if (w->method != WM_WOL && w->hid_retries < WAKE_HID_RETRIES &&
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
        w->keys_pending = false;
        w->fault_failed = true;
        w->failures++;
        if (w->cb.done) w->cb.done(w->cb.ctx, false);
    }
}
