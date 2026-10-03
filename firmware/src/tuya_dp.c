/*
 * MicroESP — Tuya cloud client and DP layer (US-0010).
 *
 * Credentials (priority): NVS (CLI "!auth <uuid> <authkey>", "!pid <pid>")
 *   > TuyaOpen license store (tuya_authorize) > include/tuya_secrets.h (gitignored)
 *   > placeholders (compile and boot, never reach the cloud).
 * Provisioning: BLE (Smart Life app) with Wi-Fi AP as fallback.
 * Reporting: core/dp_model.c decides what is due (change thresholds + throttle). The
 * app task takes a snapshot and hands it to the Tuya thread, which calls
 * tuya_iot_dp_obj_report() between two tuya_iot_yield() calls: TuyaOpen's MQTT client
 * (coreMQTT + the unlocked publish list) is not thread-safe, so every publish and
 * tuya_iot_reset() runs in the thread that runs the MQTT loop. One report in flight;
 * the result comes back as EV_REPORT_DONE. Only while MQTT is connected, 5 s back-off
 * after a failure, everything force-reported on every (re)connection.
 */
#include <stdio.h>
#include <string.h>

#include "log_redact.h"
#include "mesp_hal.h"
#include "modules.h"
#include "netmgr.h"
#include "tal_api.h"
#include "tuya_authorize.h"
#include "tuya_iot.h"
#include "tuya_iot_dp.h"
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
#include "netconn_wifi.h"
#endif
#if defined(ENABLE_LIBLWIP) && (ENABLE_LIBLWIP == 1)
#include "lwip_init.h"
#endif

#if __has_include("tuya_secrets.h")
#include "tuya_secrets.h"
#define SECRETS_SRC "tuya_secrets.h"
#else
#define SECRETS_SRC "placeholder"
#endif
#ifndef TUYA_PRODUCT_ID
#define TUYA_PRODUCT_ID "xxxxxxxxxxxxxxxx"
#endif
#ifndef TUYA_OPENSDK_UUID
#define TUYA_OPENSDK_UUID "uuidxxxxxxxxxxxxxxxx"
#endif
#ifndef TUYA_OPENSDK_AUTHKEY
#define TUYA_OPENSDK_AUTHKEY "keyxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
#endif

#define NVS_UUID "t_uuid"
#define NVS_AKEY "t_akey"
#define NVS_PID  "t_pid"

#define NVS_HAS(k) (mhal_nvs_get_str((k), probe, sizeof(probe)) == 0)

static tuya_iot_client_t s_client;
static volatile bool s_reset_req;
/* report mailbox app task -> Tuya thread (one in flight) */
typedef struct {
    uint16_t n;
    int flags;
    dp_obj_t objs[DPM_COUNT];
    char str[DPM_STR_MAX + 1]; /* the only string DP (112) */
} report_req_t;
static QUEUE_HANDLE s_report_q;
static report_req_t s_tuya_req; /* Tuya thread only */
static struct {               /* app task only */
    bool busy;
    uint32_t at;
    int n;
    uint8_t ids[DPM_COUNT];
    int32_t v[DPM_COUNT];
    bool forced[DPM_COUNT];
    char str[DPM_STR_MAX + 1];
} s_inflight;
static char s_uuid[MAX_LENGTH_UUID + 1], s_akey[MAX_LENGTH_AUTHKEY + 1], s_pid[MAX_LENGTH_PRODUCT_ID + 1];
static const char *volatile s_cred_src = SECRETS_SRC;
static volatile bool s_creds_loaded;
static uint32_t s_report_backoff_until;
static volatile bool s_started;

const char *tuya_dp_uuid(void) { return s_uuid; }
const char *tuya_dp_pid(void) { return s_pid; }
const char *tuya_dp_cred_source(void) { return s_cred_src; }

/* ------------------------------------------------------------------ credentials */
static void load_creds(void)
{
    snprintf(s_uuid, sizeof(s_uuid), "%s", TUYA_OPENSDK_UUID);
    snprintf(s_akey, sizeof(s_akey), "%s", TUYA_OPENSDK_AUTHKEY);
    snprintf(s_pid, sizeof(s_pid), "%s", TUYA_PRODUCT_ID);
    tuya_iot_license_t lic;
    if (tuya_authorize_read(&lic) == OPRT_OK && lic.uuid && lic.authkey) {
        snprintf(s_uuid, sizeof(s_uuid), "%s", lic.uuid);
        snprintf(s_akey, sizeof(s_akey), "%s", lic.authkey);
        s_cred_src = "tuya_authorize";
    }
    char u[MAX_LENGTH_UUID + 1], a[MAX_LENGTH_AUTHKEY + 1], p[MAX_LENGTH_PRODUCT_ID + 1];
    if (mhal_nvs_get_str(NVS_UUID, u, sizeof(u)) == 0 && mhal_nvs_get_str(NVS_AKEY, a, sizeof(a)) == 0) {
        snprintf(s_uuid, sizeof(s_uuid), "%s", u);
        snprintf(s_akey, sizeof(s_akey), "%s", a);
        s_cred_src = "nvs";
    }
    if (mhal_nvs_get_str(NVS_PID, p, sizeof(p)) == 0) snprintf(s_pid, sizeof(s_pid), "%s", p);
    memset(a, 0, sizeof(a));
    lr_set_secret(LR_SLOT_TUYA_AUTHKEY, s_akey); /* never in a log line, whatever logs it */
    s_creds_loaded = true;
}

/* Provisioning state for the CLI policy (app task). Conservative until the Tuya thread
 * has loaded the credentials. */
bool tuya_dp_creds_provisioned(void)
{
    char probe[MAX_LENGTH_AUTHKEY + 1];
    if (!s_creds_loaded) return true;
    if (strcmp(s_cred_src, "placeholder")) return true; /* secrets.h / license / NVS at boot */
    bool has = NVS_HAS(NVS_UUID) || NVS_HAS(NVS_AKEY);   /* written since boot */
    memset(probe, 0, sizeof(probe));
    return has;
}

bool tuya_dp_pid_provisioned(void)
{
    char probe[MAX_LENGTH_PRODUCT_ID + 1];
    if (!s_creds_loaded) return true;
    if (strcmp(TUYA_PRODUCT_ID, "xxxxxxxxxxxxxxxx")) return true; /* tuya_secrets.h */
    return NVS_HAS(NVS_PID);
}

static bool valid_token(const char *s, size_t min, size_t max)
{
    size_t n = s ? strlen(s) : 0;
    if (n < min || n > max) return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
    }
    return true;
}

int tuya_dp_set_creds(const char *uuid, const char *authkey)
{
    if (!valid_token(uuid, 16, MAX_LENGTH_UUID) || !valid_token(authkey, 16, MAX_LENGTH_AUTHKEY)) return -1;
    if (mhal_nvs_set_str(NVS_UUID, uuid) || mhal_nvs_set_str(NVS_AKEY, authkey)) return -2;
    return 0;
}

int tuya_dp_set_pid(const char *pid)
{
    if (!valid_token(pid, 8, MAX_LENGTH_PRODUCT_ID)) return -1;
    return mhal_nvs_set_str(NVS_PID, pid) ? -2 : 0;
}

int tuya_dp_clear_creds(void)
{
    int rc = mhal_nvs_erase(NVS_UUID) | mhal_nvs_erase(NVS_AKEY) | mhal_nvs_erase(NVS_PID);
    PR_NOTICE("Tuya credentials erased from NVS (%d)", rc);
    return rc ? -1 : 0;
}

void tuya_dp_factory_reset(const char *source)
{
    PR_WARN("Tuya factory reset requested by %s (activated=%d)", source, g_app.activated);
    app_toast("Reset Tuya...", 5000);
    if (!s_started) return;
    /* Executed by the Tuya thread (tuya_iot_reset touches the MQTT client and the
     * client's event/state): -> TUYA_EVENT_RESET ... RESET_COMPLETE -> reboot into
     * provisioning mode. */
    s_reset_req = true;
}

/* ------------------------------------------------------------------ cloud -> app (tuya thread) */
static void on_dp_obj(dp_obj_recv_t *dpobj)
{
    for (uint32_t i = 0; i < dpobj->dpscnt; i++) {
        dp_obj_t *dp = dpobj->dps + i;
        app_ev_t ev = {.type = EV_DP_WRITE, .a = dp->id};
        switch (dp->type) {
        case PROP_BOOL: ev.b = DPT_BOOL; ev.v = dp->value.dp_bool; break;
        case PROP_VALUE: ev.b = DPT_VALUE; ev.v = dp->value.dp_value; break;
        case PROP_ENUM: ev.b = DPT_ENUM; ev.v = (int32_t)dp->value.dp_enum; break;
        case PROP_BITMAP: ev.b = DPT_BITMAP; ev.v = (int32_t)dp->value.dp_bitmap; break;
        default:
            PR_WARN("DP %d: unsupported type %d ignored", dp->id, dp->type);
            continue;
        }
        PR_NOTICE("DP write from cloud: %d = %ld (type %d)", dp->id, (long)ev.v, dp->type);
        if (!app_post(&ev)) PR_ERR("app queue full: DP %d dropped", dp->id);
    }
}

static void post_cloud(uint8_t what)
{
    app_ev_t ev = {.type = EV_CLOUD, .a = what};
    app_post(&ev);
}

static void user_event_handler(tuya_iot_client_t *client, tuya_event_msg_t *event)
{
    PR_DEBUG("Tuya event %d (%s), heap %d", event->id, EVENT_ID2STR(event->id), tal_system_get_free_heap_size());
    switch (event->id) {
    case TUYA_EVENT_BIND_START: post_cloud(CLOUD_BIND_START); break;
    case TUYA_EVENT_ACTIVATE_SUCCESSED: post_cloud(CLOUD_ACTIVATED); break;
    case TUYA_EVENT_MQTT_CONNECTED: post_cloud(CLOUD_CONNECTED); break;
    case TUYA_EVENT_MQTT_DISCONNECT: post_cloud(CLOUD_DISCONNECTED); break;
    case TUYA_EVENT_UPGRADE_NOTIFY: {
        cJSON *v = event->value.asJSON ? cJSON_GetObjectItem(event->value.asJSON, "version") : NULL;
        PR_NOTICE("OTA upgrade notified: version %s", cJSON_IsString(v) ? v->valuestring : "?");
        app_ev_t ev = {.type = EV_OTA, .a = OTA_NOTIFY};
        if (cJSON_IsString(v)) {
            char *p = tal_malloc(16);
            if (p) {
                snprintf(p, 16, "%s", v->valuestring);
                ev.p = p;
            }
        }
        if (!app_post(&ev)) tal_free(ev.p);
        break;
    }
    case TUYA_EVENT_RESET:
        PR_WARN("Tuya reset (type %d): unbinding", event->value.asInteger);
        post_cloud(CLOUD_RESET);
        break;
    case TUYA_EVENT_RESET_COMPLETE:
        PR_WARN("Tuya reset complete -> restart");
        tal_system_reset();
        break;
    case TUYA_EVENT_DP_RECEIVE_OBJ: on_dp_obj(event->value.dpobj); break;
    case TUYA_EVENT_DP_RECEIVE_RAW: PR_WARN("raw DP ignored"); break;
    default: break;
    }
}

static void ota_cb(tuya_ota_msg_t *msg, tuya_ota_event_t *event)
{
    if (event->id == TUYA_OTA_EVENT_FAULT) {
        app_ev_t ev = {.type = EV_OTA, .a = OTA_FAULT};
        app_post(&ev);
    }
}

static bool user_network_check(void)
{
    netmgr_status_e status = NETMGR_LINK_DOWN;
    netmgr_conn_get(NETCONN_AUTO, NETCONN_CMD_STATUS, &status);
    return status != NETMGR_LINK_DOWN;
}

/* ------------------------------------------------------------------ app task side */
void tuya_dp_on_cloud(const app_ev_t *ev)
{
    uint32_t now = app_now_ms();
    switch (ev->a) {
    case CLOUD_CONNECTED:
        PR_NOTICE("cloud: MQTT connected -> report all DPs");
        g_app.cloud_connected = true;
        g_app.activated = true;
        g_app.cloud_down_since = 0;
        s_report_backoff_until = 0;
        dpm_force_all(&g_app.dpm);
        break;
    case CLOUD_DISCONNECTED:
        PR_WARN("cloud: MQTT disconnected");
        g_app.cloud_connected = false;
        g_app.cloud_down_since = now ? now : 1;
        break;
    case CLOUD_BIND_START: PR_NOTICE("cloud: bind start (provisioning)"); break;
    case CLOUD_ACTIVATED:
        PR_NOTICE("cloud: activated");
        g_app.activated = true;
        break;
    case CLOUD_RESET:
        g_app.activated = false;
        break;
    }
}

void tuya_dp_on_write(const app_ev_t *ev)
{
    int32_t v;
    uint8_t id = ev->a;
    if (dpm_decode_write(id, (dpt_t)ev->b, ev->v, &v) != 0) {
        PR_WARN("DP %u: invalid write %ld (type %u) rejected", id, (long)ev->v, ev->b);
        dpm_force(&g_app.dpm, id); /* echo the current value back */
        return;
    }
    switch (id) {
    case DP_POWER_ON:
        if (v) wake_power_on("cloud DP 102");
        dpm_set(&g_app.dpm, DP_POWER_ON, 0); /* push button semantics */
        dpm_force(&g_app.dpm, DP_POWER_ON);
        break;
    case DP_POWER_OFF:
    case DP_REBOOT: {
        pwr_action_t a = id == DP_POWER_OFF ? PWR_SHUTDOWN : PWR_REBOOT;
        if (v) {
            power_request(a, id == DP_POWER_OFF ? "cloud DP 103" : "cloud DP 104");
        } else if (g_app.pwr.st == PWR_COUNTDOWN && g_app.pwr.action == a) {
            power_cancel("cloud DP set to false");
        }
        dpm_force(&g_app.dpm, id);
        break;
    }
    case DP_WAKE_METHOD:
        wake_set_method((wake_method_t)v);
        PR_NOTICE("wake_method = %s", wake_method_name((wake_method_t)v));
        break;
    case DP_CMD_COUNTDOWN:
        power_set_countdown(v);
        PR_NOTICE("cmd_countdown = %ld s", (long)v);
        break;
    default: break;
    }
}

void tuya_dp_tick(uint32_t now)
{
    static uint32_t last_net;
    if (now - last_net >= 1000) {
        last_net = now;
        g_app.wifi_up = s_started && user_network_check();
    }
    if (s_inflight.busy) {
        /* the Tuya thread may be blocked (TLS connect, OTA); give up waiting after 60 s */
        if (now - s_inflight.at < 60000) return;
        PR_WARN("dp report: no answer from the Tuya thread in 60 s, retrying");
        s_inflight.busy = false;
        for (int i = 0; i < s_inflight.n; i++)
            if (s_inflight.forced[i]) dpm_force(&g_app.dpm, s_inflight.ids[i]);
    }
    if (!g_app.cloud_connected || !s_report_q ||
        (s_report_backoff_until && (int32_t)(now - s_report_backoff_until) < 0))
        return;
    uint8_t ids[DPM_COUNT];
    int n = dpm_collect(&g_app.dpm, now, ids, DPM_COUNT);
    if (!n) return;
    static report_req_t req; /* app task only; copied into the queue */
    bool force = false;
    memset(&req, 0, sizeof(req));
    for (int i = 0; i < n; i++) {
        const dpm_desc_t *d = dpm_desc(ids[i]);
        int k = dpm_index(ids[i]);
        s_inflight.forced[i] = g_app.dpm.slot[k].force;
        force |= s_inflight.forced[i];
        g_app.dpm.slot[k].force = false; /* a force set while in flight is kept */
        s_inflight.ids[i] = ids[i];
        int32_t v = dpm_get(&g_app.dpm, ids[i]);
        s_inflight.v[i] = v;
        req.objs[i].id = ids[i];
        switch (d->type) {
        case DPT_BOOL: req.objs[i].type = PROP_BOOL; req.objs[i].value.dp_bool = v != 0; break;
        case DPT_VALUE: req.objs[i].type = PROP_VALUE; req.objs[i].value.dp_value = v; break;
        case DPT_ENUM: req.objs[i].type = PROP_ENUM; req.objs[i].value.dp_enum = (uint32_t)v; break;
        case DPT_BITMAP: req.objs[i].type = PROP_BITMAP; req.objs[i].value.dp_bitmap = (uint32_t)v; break;
        case DPT_STR:
            req.objs[i].type = PROP_STR;
            snprintf(req.str, sizeof(req.str), "%s", dpm_get_str(&g_app.dpm, ids[i]));
            snprintf(s_inflight.str, sizeof(s_inflight.str), "%s", req.str);
            req.objs[i].value.dp_str = NULL; /* fixed up by the Tuya thread (its own copy) */
            break;
        }
    }
    req.n = (uint16_t)n;
    req.flags = force ? DP_REPT_NO_FILTER_FLAG : 0;
    s_inflight.n = n;
    s_inflight.at = now;
    if (tal_queue_post(s_report_q, &req, 0) != OPRT_OK) {
        for (int i = 0; i < n; i++)
            if (s_inflight.forced[i]) dpm_force(&g_app.dpm, ids[i]);
        return;
    }
    s_inflight.busy = true;
}

void tuya_dp_on_report_done(const app_ev_t *ev)
{
    uint32_t now = app_now_ms();
    if (!s_inflight.busy) return; /* late answer after the 60 s give-up */
    s_inflight.busy = false;
    if (ev->v == OPRT_OK) {
        for (int i = 0; i < s_inflight.n; i++)
            dpm_mark_reported_as(&g_app.dpm, s_inflight.ids[i], now, s_inflight.v[i], s_inflight.str);
        PR_DEBUG("reported %d DP(s)", s_inflight.n);
    } else {
        for (int i = 0; i < s_inflight.n; i++)
            if (s_inflight.forced[i]) dpm_force(&g_app.dpm, s_inflight.ids[i]);
        PR_WARN("dp report of %d DP(s) failed: %ld (retry in 5 s)", s_inflight.n, (long)ev->v);
        s_report_backoff_until = now + 5000;
    }
}

/* Tuya thread: pending reset / DP report, between two tuya_iot_yield() calls. */
static void tuya_thread_work(void)
{
    if (s_reset_req) {
        s_reset_req = false;
        int rc = tuya_iot_reset(&s_client);
        PR_NOTICE("tuya_iot_reset -> %d", rc);
    }
    if (s_report_q && tal_queue_fetch(s_report_q, &s_tuya_req, 0) == OPRT_OK) {
        for (int i = 0; i < s_tuya_req.n; i++)
            if (s_tuya_req.objs[i].type == PROP_STR) s_tuya_req.objs[i].value.dp_str = s_tuya_req.str;
        int rc = tuya_iot_dp_obj_report(&s_client, NULL, s_tuya_req.objs, s_tuya_req.n, s_tuya_req.flags);
        app_ev_t ev = {.type = EV_REPORT_DONE, .v = rc};
        if (!app_post(&ev)) PR_WARN("app queue full: report result dropped");
    }
}

/* ------------------------------------------------------------------ tuya thread */
void tuya_dp_run(void)
{
#if !defined(PLATFORM_UBUNTU) || (PLATFORM_UBUNTU == 0)
    tal_cli_init();
    tuya_authorize_init();
#endif
    tal_queue_create_init(&s_report_q, sizeof(report_req_t), 1);
    load_creds();
    bool placeholder = strstr(s_uuid, "xxxx") != NULL;
    PR_NOTICE("Tuya: pid=%s uuid=%.8s... (source %s)%s", s_pid, s_uuid, s_cred_src,
              placeholder ? " PLACEHOLDER credentials: provision with !auth/!pid" : "");
    int rt = tuya_iot_init(&s_client, &(const tuya_iot_config_t){
                                          .software_ver = MESP_FW_VERSION,
                                          .productkey = s_pid,
                                          .uuid = s_uuid,
                                          .authkey = s_akey,
                                          .event_handler = user_event_handler,
                                          .network_check = user_network_check,
                                          .ota_handler = ota_cb,
                                      });
    if (rt != OPRT_OK) PR_ERR("tuya_iot_init failed: %d", rt);
#if defined(ENABLE_LIBLWIP) && (ENABLE_LIBLWIP == 1)
    TUYA_LwIP_Init();
#endif
    netmgr_type_e type = 0;
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
    type |= NETCONN_WIFI;
#endif
    netmgr_init(type);
#if defined(ENABLE_WIFI) && (ENABLE_WIFI == 1)
    netmgr_conn_set(NETCONN_WIFI, NETCONN_CMD_NETCFG, &(netcfg_args_t){.type = NETCFG_TUYA_BLE | NETCFG_TUYA_WIFI_AP});
#endif
    tuya_iot_start(&s_client);
    s_started = true;
    bool act = tuya_iot_activated(&s_client);
    if (act) post_cloud(CLOUD_ACTIVATED); /* g_app belongs to the app task */
    PR_NOTICE("Tuya started (activated=%d)", act);
    for (;;) {
        tuya_iot_yield(&s_client);
        tuya_thread_work();
    }
}
