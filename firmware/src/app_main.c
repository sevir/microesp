/*
 * MicroESP firmware — entry point, event bus and application task.
 * See app.h for the threading model and firmware/README.md for the architecture.
 */
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "log_redact.h"
#include "mesp_hal.h"
#include "modules.h"
#include "tal_api.h"
#include "tkl_output.h"

#define APP_TICK_MS   20
#define APP_QUEUE_LEN 24

app_t g_app;
static QUEUE_HANDLE s_q;
static THREAD_HANDLE s_app_thread, s_boot_thread;

uint32_t app_now_ms(void) { return (uint32_t)tal_system_get_millisecond(); }

bool app_post(const app_ev_t *ev)
{
    if (!s_q) return false;
    if (tal_queue_post(s_q, (void *)ev, 0) != OPRT_OK) {
        if (ev->type == EV_LINE) tal_free(ev->p);
        return false;
    }
    return true;
}

void app_toast(const char *msg, uint32_t ms)
{
    snprintf(g_app.toast, sizeof(g_app.toast), "%s", msg);
    g_app.toast_until = app_now_ms() + ms;
}

void app_set_last_result(last_result_t r)
{
    g_app.last_result = r;
    g_app.have_last_result = true;
    dpm_set(&g_app.dpm, DP_LAST_RESULT, r);
    dpm_force(&g_app.dpm, DP_LAST_RESULT); /* report repeated results too */
    PR_NOTICE("last_result=%s", lr_name(r));
}

/* Every TuyaOpen/app log line goes to UART0 and to the "!log" RAM ring: lines with a
 * registered secret or a sensitive keyword are replaced (core/log_redact.c). */
static void log_output(const char *str)
{
    str = lr_filter(str);
    tkl_log_output(str);
    mhal_log_tee(str);
}

static void dispatch(app_ev_t *ev, uint32_t now)
{
    switch (ev->type) {
    case EV_LINE: {
        const char *l = ev->p;
        if (l[0] == '!') cli_handle(l);
        else agent_link_on_line(l, ev->len, now);
        tal_free(ev->p);
        break;
    }
    case EV_TOO_LONG: agent_link_on_too_long(now); break;
    case EV_USB: usbc_on_event(ev); break;
    case EV_CLOUD: cloud_on_event(ev); break;
    case EV_CLOUD_RX: cloud_on_rx(ev); break;
    case EV_OTA: ota_on_event(ev); break;
    case EV_CDC_DTR:
        PR_NOTICE("cdc: port %s by the host", ev->a ? "opened" : "closed");
        if (!ev->a) agent_link_on_port_closed();
        break;
    default: break;
    }
}

static void app_task(void *arg)
{
    uint32_t last_tick = 0, last_slow = 0;
    PR_NOTICE("app task running");
    for (;;) {
        app_ev_t ev;
        if (tal_queue_fetch(s_q, &ev, APP_TICK_MS) == OPRT_OK) dispatch(&ev, app_now_ms());
        mhal_app_alive(); /* software watchdog fed (HAL supervisor) */
        uint32_t now = app_now_ms();
        if (now - last_tick < APP_TICK_MS) continue;
        last_tick = now;
        button_tick(now);
        if (now - last_slow < 100) continue;
        last_slow = now;
        agent_link_tick(now);
        power_tick(now);
        wake_mod_tick(now);
        state_tick(now);
        cloud_tick(now);
        ota_tick(now);
        cli_tick(now);
        led_tick(now);
        display_tick(now);
    }
}

static void user_main(void)
{
    cJSON_InitHooks(&(cJSON_Hooks){.malloc_fn = tal_malloc, .free_fn = tal_free});
    mhal_log_init();
    /* Release: NOTICE. Development: DEBUG. Redaction applies to both, and also to the
     * ESP-IDF log lines (Wi-Fi, esp-mqtt, TLS) through the HAL tee. */
    tal_log_init(MESP_DEV ? TAL_LOG_LEVEL_DEBUG : TAL_LOG_LEVEL_NOTICE, 1024, (TAL_LOG_OUTPUT_CB)log_output);
    mhal_log_set_filter(lr_filter);

    /* USB first: the safety nets (download mode, USJ fallback) must exist even if
     * something below fails. */
    usbc_init();

    PR_NOTICE("MicroESP firmware %s cli=%s (TuyaOpen %s, board %s), reset=%s, usb mode=%d, ota part=%s",
              MESP_FW_VERSION, MESP_BUILD_FLAVOUR, OPEN_VERSION, PLATFORM_BOARD, mhal_reset_reason(),
              mhal_usb_mode(), mhal_ota_running());

    tal_kv_init(&(tal_kv_cfg_t){.seed = "vmlkasdh93dlvlcy", .key = "dflfuap134ddlduq"});
    tal_sw_timer_init();
    tal_workq_init();
    mhal_nvs_init();

    memset(&g_app, 0, sizeof(g_app));
    g_app.boot_ms = app_now_ms();
    g_app.cpu = g_app.mem = g_app.disk_free = -1;
    dpm_init(&g_app.dpm);
    tal_queue_create_init(&s_q, sizeof(app_ev_t), APP_QUEUE_LEN);

    state_init();
    power_init();
    scripts_init();
    wake_mod_init();
    agent_link_init();
    pairing_init();
    button_init();
    led_init();
    ota_init();
    display_init();
    cloud_init(); /* starts the HAL cloud task (Wi-Fi, SNTP, TuyaLink MQTT) if provisioned */

    THREAD_CFG_T cfg = {.stackDepth = 8192, .priority = THREAD_PRIO_2, .thrdname = "mesp_app"};
    tal_thread_create_and_start(&s_app_thread, NULL, NULL, app_task, NULL, &cfg);
    /* the boot thread ends here: TuyaOpen is only the OS/LVGL framework now (no tuya_iot) */
}

static void boot_thread(void *arg)
{
    user_main();
    tal_thread_delete(s_boot_thread);
    s_boot_thread = NULL;
}

/* TuyaOpen platform entry point (platform main.c -> tuya_app_main) */
void tuya_app_main(void)
{
    THREAD_CFG_T cfg = {.stackDepth = 6 * 1024, .priority = THREAD_PRIO_1, .thrdname = "tuya_app_main"};
    tal_thread_create_and_start(&s_boot_thread, NULL, NULL, boot_thread, NULL, &cfg);
}
