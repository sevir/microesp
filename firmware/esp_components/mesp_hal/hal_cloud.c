/*
 * MicroESP HAL — cloud transport: Wi-Fi station, SNTP and one MQTT-over-TLS client
 * (esp-mqtt). Protocol-agnostic: the TuyaLink specifics (topics, signed credentials,
 * JSON) live in the application (src/cloud.c + src/core/tylink.c).
 *
 * Threads:
 *   mesp_cloud (this file) : owns the connection life cycle (Wi-Fi retries, SNTP,
 *                            MQTT client create/destroy, back-off). Credentials are
 *                            built for EVERY attempt (the TuyaLink signature includes
 *                            the current time), so esp-mqtt's auto-reconnect is off and
 *                            a fresh client is created per attempt.
 *   esp-mqtt task          : network I/O; its events are forwarded to the application
 *                            callbacks, which only post to the app queue.
 *   sys_evt                : Wi-Fi / IP events (flags + callbacks, never blocks).
 * The application publishes from its own task with mhal_cloud_publish(): it only
 * queues the message in the esp-mqtt outbox under the client mutex (no network I/O in
 * the caller), so a slow network never stalls the application task.
 *
 * Secrets: the Wi-Fi password is copied into the driver configuration only (Wi-Fi
 * storage in RAM, not in the driver's NVS namespace); the MQTT password is built by
 * the application callback and wiped after esp_mqtt_client_init() copied it. Nothing
 * here logs a credential.
 */
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/threading.h"
#include "mesp_hal.h"
#include "mqtt_client.h"

static const char *TAG = "mhal_cloud";

#define MAX_SUBS        8
#define TOPIC_MAX       96
#define VALID_TIME      1700000000 /* 2023-11: anything earlier means "clock not set" */
#define CONNECT_TIMEOUT_MS 45000
#define STABLE_MS       60000      /* a connection this long resets the back-off */
#define WIFI_RETRY_MAX_MS 30000

#define EV_MQTT_UP   BIT0
#define EV_MQTT_DOWN BIT1
#define EV_WIFI      BIT2

static struct {
    char ssid[33];
    char pass[65];
    char host[64];
    uint16_t port;
    char client_id[48];
    char subs[MAX_SUBS][TOPIC_MAX];
    int nsubs;
} s_cfg;
static mhal_cloud_cbs_t s_cbs;
static bool s_started;

static EventGroupHandle_t s_eg;
static SemaphoreHandle_t s_cli_mx; /* guards s_cli for publish vs create/destroy */
static esp_mqtt_client_handle_t s_cli;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_mqtt_up, s_got_ip, s_wifi_connecting, s_time_ok;
static volatile uint32_t s_wifi_fails;
static volatile int64_t s_wifi_fail_at, s_wifi_conn_at;
static volatile int s_last_reason, s_last_err;
static volatile uint32_t s_wifi_disc, s_mqtt_conn, s_mqtt_att, s_mqtt_disc, s_next_attempt_at;
static esp_netif_t *s_netif;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* esp_wifi_connect(); a call that fails, or that never produces an event within 60 s,
 * counts as a failed attempt so that the retry loop always picks it up again */
static void wifi_connect(void)
{
    s_wifi_conn_at = esp_timer_get_time();
    s_wifi_connecting = esp_wifi_connect() == ESP_OK;
    if (!s_wifi_connecting) {
        s_wifi_fails++;
        s_wifi_fail_at = esp_timer_get_time();
    }
}

static void emit(mhal_cloud_ev_t ev, int arg)
{
    if (s_cbs.on_event) s_cbs.on_event(ev, arg);
}

/* atomically clear "MQTT up"; true if it was up (exactly one DISCONNECTED event) */
static bool take_up(void)
{
    portENTER_CRITICAL(&s_mux);
    bool was = s_mqtt_up;
    s_mqtt_up = false;
    portEXIT_CRITICAL(&s_mux);
    return was;
}

int64_t mhal_time_ms(void)
{
    if (!s_time_ok) return 0;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (tv.tv_sec < VALID_TIME) return 0;
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

void mhal_cloud_get_stats(mhal_cloud_stats_t *st)
{
    memset(st, 0, sizeof(*st));
    st->wifi_up = s_got_ip;
    st->time_synced = mhal_time_ms() != 0;
    st->mqtt_up = s_mqtt_up;
    wifi_ap_record_t ap;
    if (s_got_ip && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) st->rssi = ap.rssi;
    st->wifi_disconnects = s_wifi_disc;
    st->mqtt_connects = s_mqtt_conn;
    st->mqtt_attempts = s_mqtt_att;
    st->mqtt_disconnects = s_mqtt_disc;
    st->last_wifi_reason = s_last_reason;
    st->last_mqtt_error = s_last_err;
    int32_t left = (int32_t)(s_next_attempt_at - now_ms());
    st->next_attempt_s = (!s_mqtt_up && s_next_attempt_at && left > 0) ? (uint32_t)(left + 999) / 1000 : 0;
}

/* ------------------------------------------------------------------ mbedTLS mutexes
 * The TuyaOpen platform sdkconfig sets CONFIG_MBEDTLS_THREADING_ALT; the mutex hooks
 * used to be installed by TuyaOpen's tuya_tls_init(), which is not linked any more.
 * Without them every mutex op fails and mbedtls_ctr_drbg_seed() returns -0x34
 * (seen on the board), so esp-tls cannot open any TLS connection. */
#if defined(MBEDTLS_THREADING_ALT)
static void mx_init(mbedtls_threading_mutex_t *m)
{
    m->mutex = xSemaphoreCreateMutex();
    m->is_valid = m->mutex != NULL;
}

static void mx_free(mbedtls_threading_mutex_t *m)
{
    if (m->mutex) vSemaphoreDelete(m->mutex);
    m->mutex = NULL;
    m->is_valid = 0;
}

static int mx_lock(mbedtls_threading_mutex_t *m)
{
    if (!m->is_valid) return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    return xSemaphoreTake(m->mutex, portMAX_DELAY) == pdTRUE ? 0 : MBEDTLS_ERR_THREADING_MUTEX_ERROR;
}

static int mx_unlock(mbedtls_threading_mutex_t *m)
{
    if (!m->is_valid) return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    return xSemaphoreGive(m->mutex) == pdTRUE ? 0 : MBEDTLS_ERR_THREADING_MUTEX_ERROR;
}
#endif

/* ------------------------------------------------------------------ Wi-Fi / IP events */
static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        bool was = s_got_ip;
        s_got_ip = false;
        s_wifi_connecting = false;
        s_last_reason = d ? d->reason : -1;
        s_wifi_fails++;
        s_wifi_fail_at = esp_timer_get_time();
        if (was) {
            s_wifi_disc++;
            ESP_LOGW(TAG, "wifi: disconnected (reason %d)", s_last_reason);
            emit(MHAL_CLOUD_WIFI_DOWN, s_last_reason);
        } else if (s_wifi_fails <= 3 || s_wifi_fails % 20 == 0) {
            ESP_LOGW(TAG, "wifi: connect failed (reason %d, attempt %lu)", s_last_reason, (unsigned long)s_wifi_fails);
        }
        xEventGroupSetBits(s_eg, EV_WIFI);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        s_got_ip = true;
        s_wifi_connecting = false;
        s_wifi_fails = 0;
        ESP_LOGI(TAG, "wifi: got ip " IPSTR, IP2STR(&e->ip_info.ip));
        emit(MHAL_CLOUD_WIFI_UP, 0);
        xEventGroupSetBits(s_eg, EV_WIFI);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        if (s_got_ip) emit(MHAL_CLOUD_WIFI_DOWN, -1);
        s_got_ip = false;
        xEventGroupSetBits(s_eg, EV_WIFI);
    }
}

static int wifi_init(void)
{
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return -1;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return -1;
    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) return -1;
    char mac[13], hn[24];
    mhal_mac_hex(mac);
    snprintf(hn, sizeof(hn), "microesp-%s", mac + 6);
    esp_netif_set_hostname(s_netif, hn);
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&ic) != ESP_OK) return -1;
    esp_wifi_set_storage(WIFI_STORAGE_RAM); /* credentials live in our NVS namespace only */
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_evt, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_LOST_IP, wifi_evt, NULL, NULL);
    wifi_config_t wc = {0};
    memcpy(wc.sta.ssid, s_cfg.ssid, strnlen(s_cfg.ssid, sizeof(wc.sta.ssid)));
    memcpy(wc.sta.password, s_cfg.pass, strnlen(s_cfg.pass, sizeof(wc.sta.password)));
    /* never fall back to an open AP with the same SSID when a password is set */
    wc.sta.threshold.authmode = s_cfg.pass[0] ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    esp_wifi_set_mode(WIFI_MODE_STA);
    e = esp_wifi_set_config(WIFI_IF_STA, &wc);
    memset(&wc, 0, sizeof(wc));
    if (e != ESP_OK) return -1;
    return esp_wifi_start() == ESP_OK ? 0 : -1;
}

static void time_synced_cb(struct timeval *tv)
{
    if (!s_time_ok) ESP_LOGI(TAG, "sntp: clock set");
    s_time_ok = true;
    emit(MHAL_CLOUD_TIME_SYNC, 0);
}

static void sntp_start(void)
{
    static bool started;
    if (started) return;
    started = true;
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
#if CONFIG_LWIP_SNTP_MAX_SERVERS > 1
    esp_sntp_setservername(1, "time.google.com");
#endif
    sntp_set_time_sync_notification_cb(time_synced_cb);
    esp_sntp_init();
    ESP_LOGI(TAG, "sntp: started");
}

/* ------------------------------------------------------------------ MQTT */
static void mqtt_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t e = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        for (int i = 0; i < s_cfg.nsubs; i++) esp_mqtt_client_subscribe(e->client, s_cfg.subs[i], 1);
        portENTER_CRITICAL(&s_mux);
        s_mqtt_up = true;
        portEXIT_CRITICAL(&s_mux);
        s_mqtt_conn++;
        s_last_err = 0;
        ESP_LOGI(TAG, "mqtt: connected to %s:%u", s_cfg.host, s_cfg.port);
        emit(MHAL_CLOUD_CONNECTED, 0);
        xEventGroupSetBits(s_eg, EV_MQTT_UP);
        break;
    case MQTT_EVENT_DISCONNECTED:
        if (take_up()) {
            s_mqtt_disc++;
            ESP_LOGW(TAG, "mqtt: disconnected");
            emit(MHAL_CLOUD_DISCONNECTED, s_last_err);
        }
        xEventGroupSetBits(s_eg, EV_MQTT_DOWN);
        break;
    case MQTT_EVENT_ERROR:
        if (e->error_handle) {
            if (e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                s_last_err = e->error_handle->connect_return_code;
                ESP_LOGW(TAG, "mqtt: connection refused, return code %d", s_last_err);
            } else if (e->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                s_last_err = -1;
                ESP_LOGW(TAG, "mqtt: transport error (esp_err 0x%x, tls 0x%x, errno %d)",
                         e->error_handle->esp_tls_last_esp_err, e->error_handle->esp_tls_stack_err,
                         e->error_handle->esp_transport_sock_errno);
            }
        }
        xEventGroupSetBits(s_eg, EV_MQTT_DOWN);
        break;
    case MQTT_EVENT_PUBLISHED: emit(MHAL_CLOUD_PUBLISHED, e->msg_id); break;
    case MQTT_EVENT_DATA:
        if (e->current_data_offset == 0 && s_cbs.on_data) {
            if (e->data_len == e->total_data_len)
                s_cbs.on_data(e->topic, (size_t)e->topic_len, e->data, e->data_len);
            else
                s_cbs.on_data(e->topic, (size_t)e->topic_len, NULL, -e->total_data_len);
        }
        break;
    default: break;
    }
}

int mhal_cloud_publish(const char *topic, const char *payload, int len, int qos)
{
    if (!s_cli_mx || xSemaphoreTake(s_cli_mx, pdMS_TO_TICKS(50)) != pdTRUE) return -1;
    int id = -1;
    if (s_cli && s_mqtt_up) id = esp_mqtt_client_enqueue(s_cli, topic, payload, len, qos, 0, true);
    xSemaphoreGive(s_cli_mx);
    return id;
}

static void mqtt_teardown(void)
{
    xSemaphoreTake(s_cli_mx, portMAX_DELAY);
    esp_mqtt_client_handle_t c = s_cli;
    s_cli = NULL;
    xSemaphoreGive(s_cli_mx);
    if (take_up()) {
        s_mqtt_disc++;
        emit(MHAL_CLOUD_DISCONNECTED, s_last_err);
    }
    if (c) esp_mqtt_client_destroy(c); /* stops the esp-mqtt task; never from its own task */
    xEventGroupClearBits(s_eg, EV_MQTT_UP | EV_MQTT_DOWN);
}

static int mqtt_attempt(void)
{
    char user[160], pass[80];
    uint32_t ts = (uint32_t)(mhal_time_ms() / 1000);
    if (!s_cbs.make_auth || s_cbs.make_auth(ts, user, sizeof(user), pass, sizeof(pass)) != 0) {
        ESP_LOGE(TAG, "mqtt: no credentials");
        return -1;
    }
    esp_mqtt_client_config_t c = {
        .broker.address.hostname = s_cfg.host,
        .broker.address.port = s_cfg.port,
        .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.client_id = s_cfg.client_id,
        .credentials.username = user,
        .credentials.authentication.password = pass,
        .session.keepalive = 60,
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        .network.disable_auto_reconnect = true,
        .network.timeout_ms = 10000,
        .task.stack_size = 6144,
        .task.priority = 5,
        .buffer.size = 2048,
        .buffer.out_size = 2048,
        .outbox.limit = 16384,
    };
    xSemaphoreTake(s_cli_mx, portMAX_DELAY);
    s_cli = esp_mqtt_client_init(&c); /* copies every string */
    memset(pass, 0, sizeof(pass));
    int rc = -1;
    if (s_cli) {
        esp_mqtt_client_register_event(s_cli, ESP_EVENT_ANY_ID, mqtt_evt, NULL);
        rc = esp_mqtt_client_start(s_cli) == ESP_OK ? 0 : -1;
    }
    xSemaphoreGive(s_cli_mx);
    s_mqtt_att++;
    ESP_LOGI(TAG, "mqtt: connecting to %s:%u (attempt %lu)", s_cfg.host, s_cfg.port, (unsigned long)s_mqtt_att);
    return rc;
}

static void cloud_task(void *arg)
{
    enum { ST_IDLE, ST_CONNECTING, ST_UP } st = ST_IDLE;
    uint32_t fails = 0, since = 0;
    s_next_attempt_at = 0;
    int wrc = wifi_init();
    memset(s_cfg.pass, 0, sizeof(s_cfg.pass)); /* the driver keeps its own copy */
    if (wrc != 0) {
        ESP_LOGE(TAG, "wifi: init failed, cloud disabled");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "wifi: station started (ssid length %u)", (unsigned)strlen(s_cfg.ssid));
    for (;;) {
        EventBits_t b = xEventGroupWaitBits(s_eg, EV_MQTT_UP | EV_MQTT_DOWN | EV_WIFI, pdTRUE, pdFALSE,
                                            pdMS_TO_TICKS(500));
        uint32_t now = now_ms();
        /* Wi-Fi retries with back-off (2, 4, 8, 16, 30 s ...) */
        if (!s_got_ip && s_wifi_connecting && esp_timer_get_time() - s_wifi_conn_at > 60000000LL) {
            ESP_LOGW(TAG, "wifi: no answer to the connection attempt in 60 s");
            s_wifi_connecting = false;
            s_wifi_fails++;
            s_wifi_fail_at = esp_timer_get_time();
        }
        if (!s_got_ip && !s_wifi_connecting && s_wifi_fails) {
            uint32_t n = s_wifi_fails > 5 ? 5 : s_wifi_fails;
            uint32_t delay = 1000u << n;
            if (delay > WIFI_RETRY_MAX_MS) delay = WIFI_RETRY_MAX_MS;
            if ((esp_timer_get_time() - s_wifi_fail_at) / 1000 >= delay) wifi_connect();
        }
        if (s_got_ip) sntp_start();
        /* s_time_ok is set by the SNTP callback only: an RTC value surviving a reset is
         * behind by the reset time and must not sign credentials */
        if (!s_cfg.host[0]) continue;

        if (!s_got_ip && st != ST_IDLE) { /* link lost: drop the session, retry as soon as IP is back */
            mqtt_teardown();
            st = ST_IDLE;
            s_next_attempt_at = 0;
            continue;
        }
        switch (st) {
        case ST_IDLE:
            if (s_got_ip && mhal_time_ms() && (!s_next_attempt_at || (int32_t)(now - s_next_attempt_at) >= 0)) {
                s_next_attempt_at = 0;
                since = now;
                if (mqtt_attempt() == 0) {
                    st = ST_CONNECTING;
                } else {
                    mqtt_teardown();
                    goto backoff;
                }
            }
            break;
        case ST_CONNECTING:
            if (s_mqtt_up) {
                st = ST_UP;
                since = now;
            } else if ((b & EV_MQTT_DOWN) || now - since > CONNECT_TIMEOUT_MS) {
                mqtt_teardown();
                goto backoff;
            }
            break;
        case ST_UP:
            if (!s_mqtt_up || (b & EV_MQTT_DOWN)) {
                if (now - since >= STABLE_MS) fails = 0;
                mqtt_teardown();
                goto backoff;
            }
            break;
        }
        continue;
    backoff:
        st = ST_IDLE;
        fails++;
        {
            uint32_t delay = fails <= 5 ? 2000u << (fails - 1) : 120000u;
            s_next_attempt_at = (now + delay) ? now + delay : 1;
            ESP_LOGW(TAG, "mqtt: next attempt in %lu s (failure %lu, last error %d)", (unsigned long)(delay / 1000),
                     (unsigned long)fails, s_last_err);
        }
    }
}

int mhal_cloud_start(const mhal_cloud_cfg_t *cfg, const mhal_cloud_cbs_t *cbs)
{
    if (s_started || !cfg || !cfg->ssid || !cfg->ssid[0]) return -1;
    memset(&s_cfg, 0, sizeof(s_cfg));
    snprintf(s_cfg.ssid, sizeof(s_cfg.ssid), "%s", cfg->ssid);
    snprintf(s_cfg.pass, sizeof(s_cfg.pass), "%s", cfg->wifi_pass ? cfg->wifi_pass : "");
    if (cfg->host) {
        snprintf(s_cfg.host, sizeof(s_cfg.host), "%s", cfg->host);
        s_cfg.port = cfg->port;
        snprintf(s_cfg.client_id, sizeof(s_cfg.client_id), "%s", cfg->client_id ? cfg->client_id : "");
        for (int i = 0; i < cfg->nsubs && i < MAX_SUBS; i++) {
            snprintf(s_cfg.subs[i], TOPIC_MAX, "%s", cfg->subs[i]);
            s_cfg.nsubs++;
        }
    }
    if (cbs) s_cbs = *cbs;
#if defined(MBEDTLS_THREADING_ALT)
    mbedtls_threading_set_alt(mx_init, mx_free, mx_lock, mx_unlock);
#endif
    s_eg = xEventGroupCreate();
    s_cli_mx = xSemaphoreCreateMutex();
    if (!s_eg || !s_cli_mx) return -1;
    s_started = true;
    return xTaskCreate(cloud_task, "mesp_cloud", 6144, NULL, 4, NULL) == pdPASS ? 0 : -1;
}
