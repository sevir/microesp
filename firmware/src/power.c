/*
 * MicroESP — shutdown / reboot from the app (US-0016). Flow: core/powercmd.c.
 * DP 103/104 true -> countdown DP 112 (notice to the agent, countdown screen) ->
 * signed cmd -> ack -> last_result; cancel with a short button press, the DP set back
 * to false, or "!cancel". DP 103/104 are reported back to false when finished.
 */
#include "mesp_hal.h"
#include "modules.h"
#include "tal_api.h"

#define NVS_COUNTDOWN "countdown"

static bool cb_notice(void *c, const char *action, int in_s)
{
    PR_NOTICE("power: notice %s in %d s", action, in_s);
    return link_send_notice(&g_app.link, action, in_s);
}

static uint32_t cb_cmd(void *c, const char *action)
{
    uint32_t id = link_send_cmd(&g_app.link, action, app_now_ms());
    PR_NOTICE("power: cmd %s -> id %lu", action, (unsigned long)id);
    return id;
}

static void cb_result(void *c, pwr_action_t a, last_result_t r)
{
    PR_NOTICE("power: %s finished: %s", pwr_action_name(a), lr_name(r));
    app_set_last_result(r);
    app_toast(r == LR_OK ? "Orden aceptada" : r == LR_CANCELLED ? "Cancelado" : lr_name(r), 3000);
}

static void cb_reset_dps(void *c)
{
    dpm_set(&g_app.dpm, DP_POWER_OFF, 0);
    dpm_set(&g_app.dpm, DP_REBOOT, 0);
    dpm_force(&g_app.dpm, DP_POWER_OFF);
    dpm_force(&g_app.dpm, DP_REBOOT);
}

void power_init(void)
{
    pwr_cbs_t cb = {cb_notice, cb_cmd, cb_result, cb_reset_dps, NULL};
    pwr_init(&g_app.pwr, &cb);
    uint8_t s = 10;
    if (mhal_nvs_get_u8(NVS_COUNTDOWN, &s) != 0 || s > 60) s = 10;
    g_app.countdown_s = s;
    dpm_set(&g_app.dpm, DP_CMD_COUNTDOWN, s);
    dpm_set(&g_app.dpm, DP_POWER_OFF, 0);
    dpm_set(&g_app.dpm, DP_REBOOT, 0);
}

void power_set_countdown(int s)
{
    if (s < 0 || s > 60) return;
    g_app.countdown_s = s;
    mhal_nvs_set_u8(NVS_COUNTDOWN, (uint8_t)s);
    dpm_set(&g_app.dpm, DP_CMD_COUNTDOWN, s);
    dpm_force(&g_app.dpm, DP_CMD_COUNTDOWN);
}

void power_request(pwr_action_t a, const char *source)
{
    bool online = link_online(&g_app.link);
    PR_NOTICE("power: %s requested by %s (countdown %d s, agent_online=%d)", pwr_action_name(a), source,
              g_app.countdown_s, online);
    if (!pwr_request(&g_app.pwr, a, g_app.countdown_s, online, app_now_ms())) {
        PR_WARN("power: busy, request ignored");
        return;
    }
    if (g_app.pwr.st != PWR_IDLE) dpm_set(&g_app.dpm, a == PWR_SHUTDOWN ? DP_POWER_OFF : DP_REBOOT, 1);
}

bool power_cancel(const char *source)
{
    bool ok = pwr_cancel(&g_app.pwr);
    if (ok) PR_NOTICE("power: countdown cancelled by %s", source);
    return ok;
}

void power_tick(uint32_t now) { pwr_tick(&g_app.pwr, link_online(&g_app.link), now); }
