/*
 * MicroESP — USB composite device glue (US-0013). The device itself (descriptors,
 * TinyUSB, CDC framing, safety nets) lives in esp_components/mesp_hal/hal_usb.c; this
 * module turns its callbacks into app events and tracks "remote wakeup armed".
 *
 * hid_not_armed (DP 115 bit 2): evaluated at each bus suspend — set when the host
 * suspended us WITHOUT enabling remote wakeup (wake from S3 via HID will not work),
 * cleared by a suspend with remote wakeup enabled. It is a last-known value: while the
 * PC is running the host has not decided yet.
 */
#include <string.h>

#include "mesp_hal.h"
#include "modules.h"
#include "tal_api.h"

static const char *k_ev[] = {"mount", "umount", "suspend", "resume"};

/* --- HAL callbacks (other tasks): only post events --- */
static void cb_usb(mhal_usb_evt_t ev, bool rwu)
{
    app_ev_t e = {.type = EV_USB, .a = (uint8_t)ev, .b = rwu};
    app_post(&e);
}

static void cb_line(const char *line, size_t len)
{
    char *p = tal_malloc(len + 1);
    if (!p) return;
    memcpy(p, line, len);
    p[len] = 0;
    app_ev_t e = {.type = EV_LINE, .p = p, .len = (uint16_t)len};
    if (!app_post(&e)) PR_WARN("app queue full: CDC line dropped");
}

static void cb_too_long(void)
{
    app_ev_t e = {.type = EV_TOO_LONG};
    app_post(&e);
}

static void cb_dtr(bool dtr)
{
    app_ev_t e = {.type = EV_CDC_DTR, .a = dtr};
    app_post(&e);
}

void usbc_init(void)
{
    static const mhal_usb_cbs_t cbs = {
        .on_usb = cb_usb, .on_line = cb_line, .on_too_long = cb_too_long, .on_cdc_dtr = cb_dtr};
    mhal_usb_set_callbacks(&cbs);
    int rc = mhal_usb_start();
    PR_NOTICE("usb: mode %d (%s), serial %s", rc,
              rc == MHAL_USB_OK                 ? "TinyUSB HID+CDC"
              : rc == MHAL_USB_SKIPPED_USJ      ? "USJ requested"
              : rc == MHAL_USB_SKIPPED_CRASH    ? "USJ after crash loop"
              : rc == MHAL_USB_SKIPPED_NOMOUNT  ? "USJ after no-mount fallback"
                                                : "error",
              mhal_usb_serial());
}

bool usbc_mounted(void) { return mhal_usb_mounted(); }
bool usbc_suspended(void) { return mhal_usb_suspended(); }
bool usbc_rwu(void) { return mhal_usb_rwu_enabled(); }

void usbc_on_event(const app_ev_t *ev)
{
    g_app.usb_events++;
    PR_NOTICE("usb event: %s (remote wakeup %s)", ev->a < 4 ? k_ev[ev->a] : "?", ev->b ? "armed" : "not armed");
    switch (ev->a) {
    case MHAL_USB_SUSPEND:
        g_app.hid_not_armed = !ev->b;
        break;
    case MHAL_USB_MOUNT:
    case MHAL_USB_RESUME:
        g_app.shutdown_expected = false; /* the PC is back */
        break;
    default:
        break;
    }
}
