/* MicroESP HAL — system info, NVS (namespace "microesp") and OTA/rollback helpers. */
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mesp_hal.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "mhal_sys";
#define NS "microesp"

void mhal_random(void *buf, size_t n) { esp_fill_random(buf, n); }

void mhal_mac_hex(char out[13])
{
    uint8_t m[6];
    esp_read_mac(m, ESP_MAC_WIFI_STA);
    snprintf(out, 13, "%02x%02x%02x%02x%02x%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

uint32_t mhal_uptime_s(void) { return (uint32_t)(esp_timer_get_time() / 1000000); }
uint32_t mhal_heap_internal_free(void) { return heap_caps_get_free_size(MALLOC_CAP_INTERNAL); }
uint32_t mhal_heap_internal_min(void) { return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL); }
uint32_t mhal_psram_free(void) { return heap_caps_get_free_size(MALLOC_CAP_SPIRAM); }

const char *mhal_reset_reason(void)
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "poweron";
    case ESP_RST_SW: return "sw";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_USB: return "usb";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    default: return "other";
    }
}

/* ------------------------------------------------------------------ NVS */
static bool s_nvs_ok;

int mhal_nvs_init(void)
{
    if (s_nvs_ok) return 0;
    /* Normally already initialised by the TuyaOpen/Wi-Fi stack; never erase here. */
    esp_err_t e = nvs_flash_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s (NVS settings unavailable)", esp_err_to_name(e));
        return -1;
    }
    s_nvs_ok = true;
    return 0;
}

static int open_ns(nvs_open_mode_t mode, nvs_handle_t *h)
{
    if (!s_nvs_ok && mhal_nvs_init() != 0) return -1;
    return nvs_open(NS, mode, h) == ESP_OK ? 0 : -1;
}

int mhal_nvs_get_blob(const char *key, void *buf, size_t *len)
{
    nvs_handle_t h;
    if (open_ns(NVS_READONLY, &h)) return -1;
    esp_err_t e = nvs_get_blob(h, key, buf, len);
    nvs_close(h);
    return e == ESP_OK ? 0 : -1;
}

int mhal_nvs_set_blob(const char *key, const void *buf, size_t len)
{
    nvs_handle_t h;
    if (open_ns(NVS_READWRITE, &h)) return -1;
    esp_err_t e = nvs_set_blob(h, key, buf, len);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK ? 0 : -1;
}

int mhal_nvs_get_str(const char *key, char *buf, size_t len)
{
    nvs_handle_t h;
    if (open_ns(NVS_READONLY, &h)) return -1;
    esp_err_t e = nvs_get_str(h, key, buf, &len);
    nvs_close(h);
    return e == ESP_OK ? 0 : -1;
}

int mhal_nvs_set_str(const char *key, const char *val)
{
    nvs_handle_t h;
    if (open_ns(NVS_READWRITE, &h)) return -1;
    esp_err_t e = nvs_set_str(h, key, val);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK ? 0 : -1;
}

int mhal_nvs_get_u8(const char *key, uint8_t *v)
{
    nvs_handle_t h;
    if (open_ns(NVS_READONLY, &h)) return -1;
    esp_err_t e = nvs_get_u8(h, key, v);
    nvs_close(h);
    return e == ESP_OK ? 0 : -1;
}

int mhal_nvs_set_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    if (open_ns(NVS_READWRITE, &h)) return -1;
    esp_err_t e = nvs_set_u8(h, key, v);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e == ESP_OK ? 0 : -1;
}

int mhal_nvs_erase(const char *key)
{
    nvs_handle_t h;
    if (open_ns(NVS_READWRITE, &h)) return -1;
    esp_err_t e = nvs_erase_key(h, key);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return (e == ESP_OK || e == ESP_ERR_NVS_NOT_FOUND) ? 0 : -1;
}

/* ------------------------------------------------------------------ OTA */
int mhal_ota_state(void)
{
    esp_ota_img_states_t st;
    const esp_partition_t *p = esp_ota_get_running_partition();
    if (p && esp_ota_get_state_partition(p, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) return MHAL_OTA_PENDING;
    return MHAL_OTA_NA;
}

const char *mhal_ota_running(void)
{
    const esp_partition_t *p = esp_ota_get_running_partition();
    return p ? p->label : "?";
}

bool mhal_ota_rollback_enabled(void)
{
#ifdef CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
    return true;
#else
    return false;
#endif
}

int mhal_ota_mark_valid(void)
{
    esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "esp_ota_mark_app_valid_cancel_rollback: %s", esp_err_to_name(e));
    return e == ESP_OK ? 0 : -1;
}
