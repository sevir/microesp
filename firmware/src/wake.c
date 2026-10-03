/*
 * MicroESP — power-on glue (US-0014). Sequencing lives in core/wake_fsm.c.
 *
 * HID path when the PC is NOT mounted (S4/S5 with powered USB): there is no host to
 * accept a standard remote wakeup (the device was never configured / the host
 * controller is off). Best known approach, implemented here: drive USB resume
 * signalling (K-state, 1-15 ms) directly through the DWC2 controller
 * (dcd_remote_wakeup), 3 attempts 2 s apart. It only works if the BIOS/chipset
 * monitors the port in S4/S5 ("Wake on USB / keyboard", "Always On USB"); not yet
 * validated on the Lenovo (MESP-US-0002). WOL is the reliable fallback.
 */
#include <string.h>

#include "mesp_hal.h"
#include "modules.h"
#include "tal_api.h"

#define NVS_WAKE_METHOD "wake_m"
#define NVS_MACS        "macs"

static int cb_rwu(void *c)
{
    int rc = mhal_hid_remote_wakeup();
    PR_NOTICE("wake: HID remote wakeup -> %d", rc);
    return rc;
}

static int cb_force(void *c)
{
    int rc = mhal_hid_force_resume();
    PR_NOTICE("wake: HID forced resume signalling -> %d", rc);
    return rc;
}

static int cb_wol(void *c)
{
    int n = 0;
    for (int i = 0; i < g_app.nmacs; i++) {
        uint8_t pkt[WOL_PKT_LEN];
        wol_build(g_app.macs[i], pkt);
        if (mhal_wol_send(pkt, sizeof(pkt)) == 0) n++;
        PR_NOTICE("wake: WOL to %02x:%02x:%02x:%02x:%02x:%02x", g_app.macs[i][0], g_app.macs[i][1], g_app.macs[i][2],
                  g_app.macs[i][3], g_app.macs[i][4], g_app.macs[i][5]);
    }
    if (!g_app.nmacs) PR_WARN("wake: WOL requested but no MAC known (agent never connected)");
    return n;
}

static void cb_sent(void *c)
{
    app_set_last_result(LR_WAKE_SENT);
    app_toast("Encendiendo PC...", 3000);
}

static void cb_done(void *c, bool ok)
{
    PR_NOTICE("wake: %s", ok ? "PC is up" : "FAILED (no mount within 120 s / no target)");
    if (!ok) app_set_last_result(LR_WAKE_FAILED);
}

void wake_mod_init(void)
{
    wake_cbs_t cb = {cb_rwu, cb_force, cb_wol, cb_sent, cb_done, NULL};
    wake_init(&g_app.wake, &cb);
    uint8_t m = WM_HID_THEN_WOL;
    if (mhal_nvs_get_u8(NVS_WAKE_METHOD, &m) != 0 || m >= WM__COUNT) m = WM_HID_THEN_WOL;
    g_app.wake_method = (wake_method_t)m;
    dpm_set(&g_app.dpm, DP_WAKE_METHOD, m);
    uint8_t buf[LINK_MAX_MACS * 6];
    size_t len = sizeof(buf);
    if (mhal_nvs_get_blob(NVS_MACS, buf, &len) == 0 && len % 6 == 0) {
        g_app.nmacs = (int)(len / 6);
        memcpy(g_app.macs, buf, len);
    }
    dpm_set(&g_app.dpm, DP_POWER_ON, 0);
    PR_NOTICE("wake: method=%s, %d MAC(s) stored", wake_method_name(g_app.wake_method), g_app.nmacs);
}

void wake_set_method(wake_method_t m)
{
    g_app.wake_method = m;
    mhal_nvs_set_u8(NVS_WAKE_METHOD, (uint8_t)m);
    dpm_set(&g_app.dpm, DP_WAKE_METHOD, m);
    dpm_force(&g_app.dpm, DP_WAKE_METHOD);
}

void wake_store_macs(const uint8_t macs[][6], int n)
{
    if (n < 0 || n > LINK_MAX_MACS) return;
    if (n == g_app.nmacs && !memcmp(macs, g_app.macs, (size_t)n * 6)) return;
    if (n == 0 && g_app.nmacs) return; /* keep the last known MACs */
    memcpy(g_app.macs, macs, (size_t)n * 6);
    g_app.nmacs = n;
    mhal_nvs_set_blob(NVS_MACS, g_app.macs, (size_t)n * 6);
    PR_NOTICE("wake: %d MAC(s) stored for WOL", n);
}

static wake_usb_t usb_now(void)
{
    wake_usb_t u = {usbc_mounted(), usbc_suspended(), usbc_rwu()};
    return u;
}

wake_rc_t wake_power_on(const char *source)
{
    wake_usb_t u = usb_now();
    wake_rc_t rc = wake_request(&g_app.wake, g_app.wake_method, &u, app_now_ms());
    PR_NOTICE("wake: request from %s, method %s -> %s", source, wake_method_name(g_app.wake_method),
              rc == WAKE_STARTED     ? "started"
              : rc == WAKE_IGNORED_ON ? "ignored (PC already on)"
              : rc == WAKE_BUSY       ? "busy (wake in progress)"
                                      : "no target");
    if (rc == WAKE_IGNORED_ON) app_toast("PC ya encendido", 2000);
    return rc;
}

void wake_mod_tick(uint32_t now)
{
    wake_usb_t u = usb_now();
    wake_tick(&g_app.wake, &u, now);
}
