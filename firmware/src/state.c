/*
 * MicroESP — PC state (DP 101), agent_online (DP 108 via agent_link) and the fault
 * bitmap (DP 114). Logic: core/pc_state.c (host-tested transition table).
 */
#include "mesp_hal.h"
#include "modules.h"
#include "tal_api.h"

#define CLOUD_LOST_AFTER_MS 60000

void state_init(void)
{
    pcs_init(&g_app.pcs, app_now_ms());
    dpm_set(&g_app.dpm, DP_PC_STATE, PCS_UNKNOWN);
    dpm_set(&g_app.dpm, DP_FAULT, 0);
}

void state_tick(uint32_t now)
{
    pcs_inputs_t in = {
        .mounted = usbc_mounted(),
        .suspended = usbc_suspended(),
        .agent_online = link_online(&g_app.link),
        .wake_in_progress = g_app.wake.active,
        .shutdown_expected = g_app.shutdown_expected,
    };
    if (pcs_update(&g_app.pcs, &in, now)) {
        PR_NOTICE("pc_state -> %s (mounted=%d suspended=%d agent=%d wake=%d)", pcs_name(g_app.pcs.state), in.mounted,
                  in.suspended, in.agent_online, in.wake_in_progress);
        dpm_set(&g_app.dpm, DP_PC_STATE, g_app.pcs.state);
    }
    uint32_t f = 0;
    if (g_app.pcs.agent_lost) f |= FAULT_AGENT_LOST;
    if (g_app.wake.fault_failed) f |= FAULT_WAKE_FAILED;
    if (g_app.hid_not_armed) f |= FAULT_HID_NOT_ARMED;
    if (g_app.activated && !g_app.cloud_connected && g_app.cloud_down_since &&
        now - g_app.cloud_down_since >= CLOUD_LOST_AFTER_MS)
        f |= FAULT_CLOUD_LOST;
    if (f != g_app.faults) {
        PR_NOTICE("fault bitmap 0x%02lx -> 0x%02lx", (unsigned long)g_app.faults, (unsigned long)f);
        g_app.faults = f;
        dpm_set(&g_app.dpm, DP_FAULT, (int32_t)f);
    }
}
