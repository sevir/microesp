/*
 * MicroESP — TuyaLink cloud glue (US-0010, TuyaLink since 0.2.0; ADR-5).
 *
 * Replaces the TuyaOpen tuya_iot client (TuyaOS licences UUID/AuthKey could not be
 * obtained). Layers:
 *   core/tylink.c         pure C: credentials signature, topics, JSON encode/decode
 *   core/dp_model.c       pure C: DP table, change thresholds, throttling (unchanged)
 *   mesp_hal/hal_cloud.c  ESP-IDF: Wi-Fi STA, SNTP, esp-mqtt over TLS (own task)
 *   this file             app task: provisioning (NVS), report scheduling, commands
 *
 * Threading (same discipline as before): the HAL callbacks (esp-mqtt task, sys_evt,
 * mesp_cloud task) only post events to the app queue; every decision, every DP and
 * every publish happens in the app task. A publish only queues the message in the
 * esp-mqtt outbox (mhal_cloud_publish never does network I/O in the caller). One
 * property/report in flight at a time; it completes on its PUBACK (EV_CLOUD
 * PUBLISHED with the same msg id), fails on disconnection or after 30 s, and is then
 * retried 5 s later. Every DP is force-reported on each (re)connection.
 *
 * Credentials (NVS namespace "microesp", CLI !tylink / !wifi, applied on reboot):
 *   tl_region tl_pid tl_did tl_dsec   TuyaLink region, productId, deviceId, deviceSecret
 *   wifi_ssid wifi_pass               Wi-Fi station
 * The deviceSecret, the Wi-Fi password and each derived MQTT password are registered
 * with the log redactor and never printed.
 */
#include <stdio.h>
#include <string.h>

#include "cli_policy.h"
#include "log_redact.h"
#include "mesp_hal.h"
#include "modules.h"
#include "tal_api.h"
#include "tylink.h"

#define NVS_REGION "tl_region"
#define NVS_PID    "tl_pid"
#define NVS_DID    "tl_did"
#define NVS_DSEC   "tl_dsec"
#define NVS_SSID   "wifi_ssid"
#define NVS_WPASS  "wifi_pass"

#define REPORT_TIMEOUT_MS 30000
#define REPORT_RETRY_MS   5000
#define RX_MAX            4096

/* Loaded once at boot (before any other task uses them), read-only afterwards. */
static struct {
    char region[4], pid[TYL_ID_MAX + 1], did[TYL_ID_MAX + 1], sec[TYL_SECRET_MAX + 1];
    char ssid[CLIP_SSID_MAX + 1];
    bool tylink_ok, wifi_ok, started;
    const char *host;
} s_c;
static char s_t_report[TYL_TOPIC_MAX], s_t_set_resp[TYL_TOPIC_MAX], s_t_act_resp[TYL_TOPIC_MAX],
    s_t_model_get[TYL_TOPIC_MAX];

/* app task only */
static struct {
    bool busy;
    int msg_id;
    uint32_t at;
    int n;
    uint8_t ids[DPM_COUNT];
    int32_t v[DPM_COUNT];
    bool forced[DPM_COUNT];
    char str[DPM_STR_MAX + 1];
} s_inflight;
static uint32_t s_backoff_until, s_msg_counter;
static struct {
    uint32_t rx_cmds, rx_rejected, rx_malformed, actions, model_resp, report_resp, report_resp_err;
    uint32_t reports_ok, reports_failed, last_report_ms;
    int last_report_code;
} s_st;

/* ------------------------------------------------------------------ provisioning */
static bool nvs_has(const char *key)
{
    char probe[TYL_SECRET_MAX + 1];
    bool has = mhal_nvs_get_str(key, probe, sizeof(probe)) == 0 && probe[0];
    memset(probe, 0, sizeof(probe));
    return has;
}

bool cloud_tylink_provisioned(void)
{
    return nvs_has(NVS_REGION) && nvs_has(NVS_PID) && nvs_has(NVS_DID) && nvs_has(NVS_DSEC);
}

bool cloud_wifi_provisioned(void) { return nvs_has(NVS_SSID); }

int cloud_set_tylink(const char *region, const char *pid, const char *did, const char *secret)
{
    if (!tyl_broker_host(region) || !tyl_valid_id(pid) || !tyl_valid_id(did) || !tyl_valid_secret(secret)) return -1;
    if (mhal_nvs_set_str(NVS_REGION, region) || mhal_nvs_set_str(NVS_PID, pid) || mhal_nvs_set_str(NVS_DID, did) ||
        mhal_nvs_set_str(NVS_DSEC, secret))
        return -2;
    return 0;
}

int cloud_set_wifi(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0] || strlen(ssid) > CLIP_SSID_MAX || !pass || strlen(pass) < 8 ||
        strlen(pass) > CLIP_WIFI_PASS_MAX)
        return -1;
    if (mhal_nvs_set_str(NVS_SSID, ssid) || mhal_nvs_set_str(NVS_WPASS, pass)) return -2;
    return 0;
}

int cloud_clear_tylink(void)
{
    int rc = mhal_nvs_erase(NVS_REGION) | mhal_nvs_erase(NVS_PID) | mhal_nvs_erase(NVS_DID) | mhal_nvs_erase(NVS_DSEC);
    PR_NOTICE("TuyaLink credentials erased from NVS (%d)", rc);
    return rc ? -1 : 0;
}

int cloud_clear_wifi(void)
{
    int rc = mhal_nvs_erase(NVS_SSID) | mhal_nvs_erase(NVS_WPASS);
    PR_NOTICE("Wi-Fi settings erased from NVS (%d)", rc);
    return rc ? -1 : 0;
}

/* "6c12...ab": enough to recognise the device, not the whole id */
static const char *masked_did(void)
{
    static char m[16];
    size_t n = strlen(s_c.did);
    if (n < 8) snprintf(m, sizeof(m), "-");
    else snprintf(m, sizeof(m), "%.4s...%s", s_c.did, s_c.did + n - 2);
    return m;
}

/* ------------------------------------------------------------------ HAL callbacks (other tasks) */
static int make_auth(uint32_t ts, char *user, size_t ulen, char *pass, size_t plen)
{
    if (plen < TYL_PASS_HEX + 1 || tyl_username(s_c.did, ts, user, ulen) < 0) return -1;
    if (tyl_password(s_c.sec, s_c.did, ts, pass) != 0) return -1;
    lr_set_secret(LR_SLOT_MQTT_PASS, pass);
    return 0;
}

static void on_hal_event(mhal_cloud_ev_t ev, int arg)
{
    app_ev_t e = {.type = EV_CLOUD, .a = (uint8_t)ev, .v = arg};
    if (!app_post(&e)) PR_WARN("app queue full: cloud event %d dropped", ev);
}

static void on_hal_data(const char *topic, size_t tlen, const char *data, int len)
{
    tyl_topic_t k = tyl_topic_kind(topic, tlen, s_c.did);
    if (k == TYL_T_UNKNOWN) return;
    app_ev_t e = {.type = EV_CLOUD_RX, .a = (uint8_t)k, .v = len};
    /* model/get_response (the whole thing model) is only counted, never copied */
    if (len > 0 && len <= RX_MAX && k != TYL_T_MODEL_GET_RESP) {
        char *p = tal_malloc((size_t)len + 1);
        if (!p) return;
        memcpy(p, data, (size_t)len);
        p[len] = 0;
        e.p = p;
        e.len = (uint16_t)len;
    }
    if (!app_post(&e) && e.p) tal_free(e.p);
}

/* ------------------------------------------------------------------ boot */
void cloud_init(void)
{
    char wpass[CLIP_WIFI_PASS_MAX + 1] = "";
    memset(&s_c, 0, sizeof(s_c));
    s_c.tylink_ok = mhal_nvs_get_str(NVS_REGION, s_c.region, sizeof(s_c.region)) == 0 &&
                    mhal_nvs_get_str(NVS_PID, s_c.pid, sizeof(s_c.pid)) == 0 &&
                    mhal_nvs_get_str(NVS_DID, s_c.did, sizeof(s_c.did)) == 0 &&
                    mhal_nvs_get_str(NVS_DSEC, s_c.sec, sizeof(s_c.sec)) == 0;
    s_c.host = s_c.tylink_ok ? tyl_broker_host(s_c.region) : NULL;
    if (s_c.tylink_ok && (!s_c.host || !tyl_valid_id(s_c.did) || !tyl_valid_secret(s_c.sec))) {
        PR_ERR("cloud: stored TuyaLink settings are invalid, ignored (re-provision with !tylink)");
        s_c.tylink_ok = false;
        s_c.host = NULL;
    }
    s_c.wifi_ok = mhal_nvs_get_str(NVS_SSID, s_c.ssid, sizeof(s_c.ssid)) == 0 && s_c.ssid[0] &&
                  mhal_nvs_get_str(NVS_WPASS, wpass, sizeof(wpass)) == 0;
    lr_set_secret(LR_SLOT_TYLINK_SECRET, s_c.tylink_ok ? s_c.sec : NULL);
    lr_set_secret(LR_SLOT_WIFI_PASS, wpass);

    g_app.cloud_provisioned = s_c.tylink_ok && s_c.wifi_ok;
    if (g_app.cloud_provisioned) g_app.cloud_down_since = app_now_ms() | 1; /* cloud_lost after 60 s without MQTT */
    PR_NOTICE("cloud: TuyaLink %s (region=%s pid=%s device=%s), wifi %s", s_c.tylink_ok ? "provisioned" : "NOT provisioned",
              s_c.tylink_ok ? s_c.region : "-", s_c.tylink_ok ? s_c.pid : "-", masked_did(),
              s_c.wifi_ok ? "configured" : "NOT configured");
    if (!s_c.wifi_ok) {
        PR_NOTICE("cloud: provision with !wifi <ssid> <password> and !tylink <region> <productId> <deviceId> <deviceSecret>");
        memset(wpass, 0, sizeof(wpass));
        return;
    }
    static char cid[48], subs[4][TYL_TOPIC_MAX];
    static const char *sub_ptrs[4];
    mhal_cloud_cfg_t cfg = {.ssid = s_c.ssid, .wifi_pass = wpass};
    if (s_c.tylink_ok) {
        tyl_client_id(s_c.did, cid, sizeof(cid));
        tyl_topic(s_c.did, "property/report", s_t_report, sizeof(s_t_report));
        tyl_topic(s_c.did, "property/set_response", s_t_set_resp, sizeof(s_t_set_resp));
        tyl_topic(s_c.did, "action/execute_response", s_t_act_resp, sizeof(s_t_act_resp));
        tyl_topic(s_c.did, "model/get", s_t_model_get, sizeof(s_t_model_get));
        const char *sfx[4] = {"property/set", "action/execute", "model/get_response", "property/report_response"};
        for (int i = 0; i < 4; i++) {
            tyl_topic(s_c.did, sfx[i], subs[i], sizeof(subs[i]));
            sub_ptrs[i] = subs[i];
        }
        cfg.host = s_c.host;
        cfg.port = TYL_PORT;
        cfg.client_id = cid;
        cfg.subs = sub_ptrs;
        cfg.nsubs = 4;
    }
    static const mhal_cloud_cbs_t cbs = {.on_event = on_hal_event, .on_data = on_hal_data, .make_auth = make_auth};
    int rc = mhal_cloud_start(&cfg, &cbs);
    memset(wpass, 0, sizeof(wpass)); /* the HAL copied it */
    s_c.started = rc == 0;
    if (rc) PR_ERR("cloud: start failed (%d)", rc);
}

/* ------------------------------------------------------------------ app task */
static void inflight_failed(const char *why)
{
    if (!s_inflight.busy) return;
    s_inflight.busy = false;
    for (int i = 0; i < s_inflight.n; i++)
        if (s_inflight.forced[i]) dpm_force(&g_app.dpm, s_inflight.ids[i]);
    s_st.reports_failed++;
    s_backoff_until = app_now_ms() + REPORT_RETRY_MS;
    PR_WARN("property/report of %d DP(s) failed (%s), retry in 5 s", s_inflight.n, why);
}

static void publish_json(const char *topic, const char *json, int len)
{
    if (mhal_cloud_publish(topic, json, len, 1) < 0) PR_WARN("cloud: publish to %s dropped (not connected)", topic);
}

static void next_msgid(char out[TYL_MSGID_MAX + 1])
{
    uint32_t r;
    mhal_random(&r, sizeof(r));
    tyl_msgid(++s_msg_counter, r, out);
}

void cloud_on_event(const app_ev_t *ev)
{
    uint32_t now = app_now_ms();
    switch ((mhal_cloud_ev_t)ev->a) {
    case MHAL_CLOUD_WIFI_UP:
        g_app.wifi_up = true;
        PR_NOTICE("cloud: wifi up");
        break;
    case MHAL_CLOUD_WIFI_DOWN:
        g_app.wifi_up = false;
        PR_WARN("cloud: wifi down (reason %ld)", (long)ev->v);
        break;
    case MHAL_CLOUD_TIME_SYNC: PR_NOTICE("cloud: clock synchronised (SNTP)"); break;
    case MHAL_CLOUD_CONNECTED: {
        PR_NOTICE("cloud: TuyaLink MQTT connected -> report all DPs");
        g_app.cloud_connected = true;
        g_app.cloud_down_since = 0;
        s_backoff_until = 0;
        s_inflight.busy = false;
        dpm_force_all(&g_app.dpm);
        char id[TYL_MSGID_MAX + 1], buf[128];
        next_msgid(id);
        int n = tyl_build_model_get(id, mhal_time_ms(), buf, sizeof(buf));
        if (n > 0) publish_json(s_t_model_get, buf, n);
        break;
    }
    case MHAL_CLOUD_DISCONNECTED:
        PR_WARN("cloud: TuyaLink MQTT disconnected (last error %ld)", (long)ev->v);
        g_app.cloud_connected = false;
        g_app.cloud_down_since = now | 1;
        inflight_failed("disconnected");
        break;
    case MHAL_CLOUD_PUBLISHED:
        if (s_inflight.busy && ev->v == s_inflight.msg_id) {
            s_inflight.busy = false;
            for (int i = 0; i < s_inflight.n; i++)
                dpm_mark_reported_as(&g_app.dpm, s_inflight.ids[i], now, s_inflight.v[i], s_inflight.str);
            s_st.reports_ok++;
            s_st.last_report_ms = now;
            PR_DEBUG("property/report of %d DP(s) acknowledged", s_inflight.n);
        }
        break;
    }
}

/* A validated property write from the cloud (id and value already range-checked). */
static void apply_write(uint8_t id, int32_t v)
{
    switch (id) {
    case DP_POWER_ON:
        if (v) wake_power_on("cloud power_on");
        dpm_set(&g_app.dpm, DP_POWER_ON, 0); /* push button semantics */
        dpm_force(&g_app.dpm, DP_POWER_ON);
        break;
    case DP_POWER_OFF:
    case DP_REBOOT: {
        pwr_action_t a = id == DP_POWER_OFF ? PWR_SHUTDOWN : PWR_REBOOT;
        if (v) {
            power_request(a, id == DP_POWER_OFF ? "cloud power_off" : "cloud reboot");
        } else if (g_app.pwr.st == PWR_COUNTDOWN && g_app.pwr.action == a) {
            power_cancel("cloud property set to false");
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

void cloud_on_rx(const app_ev_t *ev)
{
    char buf[128], id[TYL_MSGID_MAX + 1];
    const char *p = ev->p;
    switch ((tyl_topic_t)ev->a) {
    case TYL_T_PROP_SET: {
        static tyl_set_t set; /* app task only */
        if (!p || tyl_parse_set(p, ev->len, &set) != 0) {
            s_st.rx_malformed++;
            PR_WARN("cloud: malformed property/set ignored (%ld bytes)", (long)ev->v);
            break;
        }
        s_st.rx_cmds++;
        for (int i = 0; i < set.nwrites; i++) {
            PR_NOTICE("cloud: set %s = %ld", dpm_desc(set.w[i].id)->code, (long)set.w[i].v);
            apply_write(set.w[i].id, set.w[i].v);
        }
        if (set.nrejected) {
            s_st.rx_rejected += (uint32_t)set.nrejected;
            PR_WARN("cloud: property/set: %d propert%s rejected (first: %.23s, %s)", set.nrejected,
                    set.nrejected == 1 ? "y" : "ies", set.first_reject_code, tyl_wres_name(set.first_reject));
        }
        int n = tyl_build_response(set.msgid, mhal_time_ms(), set.nrejected ? TYL_CODE_FAIL : TYL_CODE_OK, buf,
                                   sizeof(buf));
        if (n > 0) publish_json(s_t_set_resp, buf, n);
        break;
    }
    case TYL_T_ACTION_EXEC:
        s_st.actions++;
        if (p && tyl_parse_msgid(p, ev->len, id) == 0) {
            PR_WARN("cloud: action/execute not supported (no actions in the thing model)");
            int n = tyl_build_response(id, mhal_time_ms(), TYL_CODE_FAIL, buf, sizeof(buf));
            if (n > 0) publish_json(s_t_act_resp, buf, n);
        }
        break;
    case TYL_T_MODEL_GET_RESP:
        s_st.model_resp++;
        PR_NOTICE("cloud: model/get_response received (%ld bytes)", (long)(ev->v < 0 ? -ev->v : ev->v));
        break;
    case TYL_T_REPORT_RESP: {
        int code = -1;
        s_st.report_resp++;
        if (p && tyl_parse_code(p, ev->len, &code) == 0) s_st.last_report_code = code;
        if (code != 0) {
            s_st.report_resp_err++;
            PR_WARN("cloud: property/report rejected by the cloud (code %d)", code);
        }
        break;
    }
    default: break;
    }
    if (ev->p) tal_free(ev->p);
}

void cloud_tick(uint32_t now)
{
    if (s_inflight.busy) {
        if (now - s_inflight.at < REPORT_TIMEOUT_MS) return;
        inflight_failed("no PUBACK in 30 s");
    }
    if (!g_app.cloud_connected || (s_backoff_until && (int32_t)(now - s_backoff_until) < 0)) return;
    uint8_t ids[DPM_COUNT];
    int n = dpm_collect(&g_app.dpm, now, ids, DPM_COUNT);
    if (!n) return;
    int64_t t = mhal_time_ms();
    if (!t) return; /* never happens while connected (the clock is needed to connect) */
    tyl_prop_t props[DPM_COUNT];
    for (int i = 0; i < n; i++) {
        int k = dpm_index(ids[i]);
        s_inflight.forced[i] = g_app.dpm.slot[k].force;
        g_app.dpm.slot[k].force = false; /* a force set while in flight is kept */
        s_inflight.ids[i] = ids[i];
        s_inflight.v[i] = dpm_get(&g_app.dpm, ids[i]);
        props[i] = (tyl_prop_t){ids[i], s_inflight.v[i], NULL};
        if (dpm_desc(ids[i])->type == DPT_STR) {
            snprintf(s_inflight.str, sizeof(s_inflight.str), "%s", dpm_get_str(&g_app.dpm, ids[i]));
            props[i].s = s_inflight.str;
        }
    }
    s_inflight.n = n;
    s_inflight.at = now;
    static char json[1536]; /* app task only; esp-mqtt copies it into the outbox */
    char mid[TYL_MSGID_MAX + 1];
    next_msgid(mid);
    int len = tyl_build_report(props, n, mid, t, json, sizeof(json));
    int id = len > 0 ? mhal_cloud_publish(s_t_report, json, len, 1) : -1;
    s_inflight.busy = true; /* so that inflight_failed() restores the forced flags */
    if (id < 0) {
        inflight_failed(len > 0 ? "not queued" : "encode error");
        return;
    }
    s_inflight.msg_id = id;
}

/* ------------------------------------------------------------------ status (!status) */
void cloud_print_status(void)
{
    mhal_cloud_stats_t h;
    mhal_cloud_get_stats(&h);
    uint32_t now = app_now_ms();
    char ip[16] = "-";
    mhal_ip(ip);
    mhal_cdc_printf("tylink: region=%s host=%s product=%s device=%s provisioned=%d mqtt=%s attempts=%lu connects=%lu "
                    "disconnects=%lu last_err=%d next_try=%lus\r\n",
                    s_c.tylink_ok ? s_c.region : "-", s_c.host ? s_c.host : "-", s_c.tylink_ok ? s_c.pid : "-",
                    masked_did(), g_app.cloud_provisioned,
                    h.mqtt_up ? "connected" : s_c.tylink_ok && s_c.wifi_ok ? "connecting" : "off",
                    (unsigned long)h.mqtt_attempts, (unsigned long)h.mqtt_connects, (unsigned long)h.mqtt_disconnects,
                    h.last_mqtt_error, (unsigned long)h.next_attempt_s);
    if (s_st.last_report_ms)
        mhal_cdc_printf("tylink: reports ok=%lu failed=%lu last=%lus ago dp_reports=%lu cloud_report_errors=%lu "
                        "rx_cmds=%lu rx_rejected=%lu rx_malformed=%lu actions=%lu model_resp=%lu\r\n",
                        (unsigned long)s_st.reports_ok, (unsigned long)s_st.reports_failed,
                        (unsigned long)((now - s_st.last_report_ms) / 1000), (unsigned long)g_app.dpm.total_reports,
                        (unsigned long)s_st.report_resp_err, (unsigned long)s_st.rx_cmds,
                        (unsigned long)s_st.rx_rejected, (unsigned long)s_st.rx_malformed,
                        (unsigned long)s_st.actions, (unsigned long)s_st.model_resp);
    else
        mhal_cdc_printf("tylink: reports ok=0 failed=%lu last=never rx_cmds=%lu model_resp=%lu\r\n",
                        (unsigned long)s_st.reports_failed, (unsigned long)s_st.rx_cmds,
                        (unsigned long)s_st.model_resp);
    mhal_cdc_printf("wifi: ssid=%s configured=%d up=%d ip=%s rssi=%d disconnects=%lu last_reason=%d time_synced=%d\r\n",
                    s_c.wifi_ok ? s_c.ssid : "-", s_c.wifi_ok, h.wifi_up, ip, h.rssi,
                    (unsigned long)h.wifi_disconnects, h.last_wifi_reason, h.time_synced);
}
