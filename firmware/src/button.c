/*
 * MicroESP — BOOT button actions (US-0012). Gestures: core/button_fsm.c.
 *
 *   short          : cancel the running shutdown/reboot countdown, else next screen
 *   double         : power on the PC (wake method DP 110)
 *   hold 3..5 s    : agent pairing mode (code on screen, 120 s)
 *   hold 5..10 s   : provisioning window: !auth / !pid accepted on the CDC for 120 s
 *                    in release builds (cli.c, core/cli_policy.c)
 *   hold 10 s      : Tuya factory reset (unbind; reboots into BLE/AP provisioning)
 *   hold >= 20 s   : ROM download mode (handled by the HAL supervisor, works even if
 *                    the application is stuck)
 * ROM download mode is otherwise entered with "!dfu" or a 1200-baud touch on the CDC.
 */
#include "mesp_hal.h"
#include "modules.h"
#include "tal_api.h"

void button_init(void)
{
    mhal_button_init();
    btn_init(&g_app.btn, app_now_ms());
}

void button_tick(uint32_t now)
{
    btn_ev_t e = btn_update(&g_app.btn, mhal_button_pressed(), now);
    if (e == BTN_NONE) return;
    PR_NOTICE("button: %s", btn_ev_name(e));
    switch (e) {
    case BTN_SHORT:
        if (!power_cancel("button")) display_next_screen();
        break;
    case BTN_DOUBLE: wake_power_on("button double press"); break;
    case BTN_LONG3: pairing_start("button 3 s"); break;
    case BTN_LONG5: cli_prov_window_open(); break;
    case BTN_LONG10: tuya_dp_factory_reset("button 10 s"); break;
    case BTN_HOLD_3S: app_toast("Suelta: emparejar", 2000); break;
    case BTN_HOLD_5S: app_toast("Suelta: aprovisionar", 5000); break;
    case BTN_HOLD_10S: app_toast("Suelta: reset Tuya", 10000); break;
    case BTN_HOLD_20S: app_toast("Modo descarga", 5000); break;
    default: break;
    }
}
