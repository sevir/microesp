/*
 * MicroESP — OTA (US-0011).
 *
 * Cloud OTA: NOT available since the switch to TuyaLink (0.2.0): TuyaOpen's tuya_iot
 * client handled TUYA_EVENT_UPGRADE_NOTIFY; the TuyaLink OTA topics are not
 * implemented yet (EV_OTA is kept for that). Updates go over USB (tools/flash.sh).
 * The dual 7.4 MB OTA table and the rollback check below stay in place.
 * App rollback is enabled in the bootloader (sdkconfig.microesp:
 * CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE). A new image boots as PENDING_VERIFY; this
 * module marks it valid after a health check:
 *     >= 30 s up AND cloud MQTT connected            (cloud provisioned)
 *     >= 30 s up AND (cloud connected OR USB mounted) (not provisioned: serial/dev flash)
 * A provisioned device would receive images from the cloud, so the new image must prove
 * it can reach the cloud again (and thus receive the next OTA); USB alone is not
 * enough there.
 * If the check does not pass within 10 min, it restarts without marking: the
 * bootloader then rolls back to the previous image. A crash before marking also
 * rolls back.
 */
#include <stdio.h>
#include <string.h>

#include "mesp_hal.h"
#include "modules.h"
#include "tal_api.h"

#define HEALTH_MIN_UP_MS   30000
#define HEALTH_DEADLINE_MS 600000

static bool s_pending;
static const char *s_status = "idle";

void ota_init(void)
{
    s_pending = mhal_ota_state() == MHAL_OTA_PENDING;
    s_status = s_pending ? "pending-verify" : "valid";
    PR_NOTICE("ota: running %s, rollback %s, image %s", mhal_ota_running(),
              mhal_ota_rollback_enabled() ? "enabled" : "DISABLED", s_status);
}

const char *ota_status(void) { return s_status; }

void ota_on_event(const app_ev_t *ev)
{
    if (ev->a == OTA_NOTIFY) {
        g_app.ota_running = true;
        snprintf(g_app.ota_version, sizeof(g_app.ota_version), "%s", ev->p ? (const char *)ev->p : "?");
        s_status = "downloading";
        app_toast("Actualizando...", 60000);
        PR_NOTICE("ota: downloading %s", g_app.ota_version);
    } else if (ev->a == OTA_FAULT) {
        g_app.ota_running = false;
        s_status = "failed";
        app_toast("OTA fallida", 5000);
        PR_ERR("ota: download failed");
    }
    if (ev->p) tal_free(ev->p);
}

void ota_tick(uint32_t now)
{
    if (!s_pending) return;
    uint32_t up = now - g_app.boot_ms;
    bool healthy = g_app.cloud_connected || (!g_app.cloud_provisioned && usbc_mounted());
    if (up >= HEALTH_MIN_UP_MS && healthy) {
        s_pending = false;
        int rc = mhal_ota_mark_valid();
        s_status = rc == 0 ? "valid" : "mark-failed";
        PR_NOTICE("ota: health check passed (cloud=%d usb=%d) -> image marked valid (%d)", g_app.cloud_connected,
                  usbc_mounted(), rc);
    } else if (up >= HEALTH_DEADLINE_MS) {
        PR_ERR("ota: health check failed for 10 min -> restart (bootloader rolls back)");
        mhal_request(MHAL_ACT_REBOOT);
        s_pending = false;
    }
}
