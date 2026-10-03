/*
 * MicroESP firmware — application context and internal event bus.
 *
 * Threads:
 *   tuya_app_main  : TuyaOpen init + tuya_iot_yield() loop (cloud, BLE/AP netcfg, OTA)
 *   mesp_app       : the application task. Owns ALL application state; consumes the
 *                    event queue (app_post) and runs the periodic tick (20 ms).
 *   mesp_ui        : LVGL rendering from a snapshot (display.c)
 *   HAL tasks      : TinyUSB, CDC line dispatcher, safety supervisor (esp_components/mesp_hal)
 * Other threads never touch application state: they post events.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "button_fsm.h"
#include "dp_model.h"
#include "link_proto.h"
#include "mesp_version.h"
#include "pc_state.h"
#include "powercmd.h"
#include "wake_fsm.h"

typedef enum {
    EV_LINE = 1,  /* p = malloc'd CDC line, len */
    EV_TOO_LONG,  /* CDC line > 511 bytes discarded */
    EV_USB,       /* a = mhal_usb_evt_t, b = remote wakeup armed */
    EV_DP_WRITE,  /* a = dp id, b = dpt_t, v = value */
    EV_CLOUD,     /* a = CLOUD_* */
    EV_OTA,       /* a = OTA_* , v = extra */
    EV_CDC_DTR,   /* a = DTR state (port opened / closed by the host) */
    EV_REPORT_DONE, /* v = tuya_iot_dp_obj_report() result (report sent by the Tuya thread) */
} app_ev_type_t;

enum { CLOUD_DISCONNECTED = 0, CLOUD_CONNECTED, CLOUD_BIND_START, CLOUD_ACTIVATED, CLOUD_RESET };
enum { OTA_NOTIFY = 0, OTA_FAULT };

typedef struct {
    uint8_t type, a, b;
    uint16_t len;
    int32_t v;
    void *p;
} app_ev_t;

/* Thread-safe; never blocks. Returns false if the queue is full. */
bool app_post(const app_ev_t *ev);
uint32_t app_now_ms(void);

typedef struct {
    /* modules (all owned by the app task) */
    link_t link;
    pwr_t pwr;
    wake_t wake;
    pcs_sm_t pcs;
    dp_model_t dpm;
    btn_fsm_t btn;
    /* settings (NVS) */
    wake_method_t wake_method;
    int countdown_s;
    uint8_t macs[LINK_MAX_MACS][6];
    int nmacs;
    /* live data */
    char hostname[LINK_MAX_HOST + 1];
    int cpu, mem, disk_free; /* tenths of %, -1 = unknown */
    uint32_t pc_uptime;
    uint32_t tele_count;
    last_result_t last_result;
    bool have_last_result;
    uint32_t faults; /* DP 115 bits */
    bool hid_not_armed;
    bool shutdown_expected;
    /* cloud */
    bool cloud_connected, activated, wifi_up;
    uint32_t cloud_down_since;
    bool ota_running;
    char ota_version[16];
    /* misc */
    uint32_t boot_ms;
    uint32_t usb_events;
    int ui_screen;            /* 0 status, 1 telemetry */
    uint32_t toast_until;
    char toast[32];
} app_t;

extern app_t g_app;

void app_toast(const char *msg, uint32_t ms);
void app_set_last_result(last_result_t r);
