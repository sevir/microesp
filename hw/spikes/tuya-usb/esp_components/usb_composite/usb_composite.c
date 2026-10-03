/*
 * MicroESP spike (MESP-US-0003): composite USB device on the ESP32-S3 USB-OTG
 * peripheral, hosted inside a TuyaOpen application.
 *
 *   - HID boot keyboard, configuration bmAttributes with REMOTE_WAKEUP
 *   - CDC ACM: line echo + "!" service commands
 *   - VID 0x303A / PID 0x4002, product "MicroESP", serial "MESP-<wifi mac>"
 *
 * Re-flash without the BOOT button (TinyUSB takes the PHY away from USB-Serial/JTAG):
 *   - "!dfu" over CDC, or 1200-baud touch (open at 1200 bps and drop DTR), or
 *     BOOT button held >= 2 s at runtime:
 *       PHY is routed back to USB-Serial/JTAG + RTC_CNTL_FORCE_DOWNLOAD_BOOT + restart
 *       -> ROM download mode on 303a:1001 (/dev/ttyACM*), esptool --before no_reset.
 *   - "!usj": reboot once WITHOUT TinyUSB (USB-Serial/JTAG console, normal esptool).
 *   - Safety fallback: if the host does not mount the device within
 *     USB_MOUNT_TIMEOUT_S, or (optionally) no CDC command arrives within
 *     USB_NO_CMD_TIMEOUT_S, reboot once in USJ mode.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_wifi.h"
#include "esp_bt.h"
#include "driver/gpio.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_cntl_struct.h"
#include "hal/usb_serial_jtag_ll.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "class/hid/hid_device.h"
#include "usb_composite.h"

#ifndef USB_MOUNT_TIMEOUT_S
#define USB_MOUNT_TIMEOUT_S 20
#endif
#ifndef USB_NO_CMD_TIMEOUT_S
#define USB_NO_CMD_TIMEOUT_S 0 /* 0 = disabled */
#endif
#define USB_FW_TAG "usb-spike-1"

static const char *TAG = "usbc";

/* ------------------------------------------------------------ descriptors */
enum { ITF_HID = 0, ITF_CDC_CTRL, ITF_CDC_DATA, ITF_TOTAL };
#define EP_HID_IN    0x81
#define EP_CDC_NOTIF 0x82
#define EP_CDC_OUT   0x03
#define EP_CDC_IN    0x83

static const uint8_t s_hid_report_desc[] = {TUD_HID_REPORT_DESC_KEYBOARD()};

#define CFG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)
static const uint8_t s_cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CFG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_HID, 4, HID_ITF_PROTOCOL_KEYBOARD, sizeof(s_hid_report_desc), EP_HID_IN, 8, 10),
    TUD_CDC_DESCRIPTOR(ITF_CDC_CTRL, 5, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
};

static const tusb_desc_device_t s_dev_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static char s_serial[24];
static const char *s_strings[] = {
    (const char[]){0x09, 0x04}, /* 0: English (US) */
    "MicroESP",                 /* 1: manufacturer */
    "MicroESP",                 /* 2: product */
    s_serial,                   /* 3: serial MESP-<mac> */
    "MicroESP Keyboard",        /* 4: HID */
    "MicroESP CDC",             /* 5: CDC */
};

/* ------------------------------------------------------------ state */
RTC_NOINIT_ATTR static uint32_t s_skip_usb_magic;
RTC_NOINIT_ATTR static uint32_t s_crash_count;
#define SKIP_USB_MAGIC 0x55534A31 /* "USJ1" */

static volatile bool s_mounted, s_suspended, s_rwu_enabled, s_cmd_seen;
static volatile uint32_t s_bitrate = 115200;
static volatile int s_pending_action; /* 1 = download mode, 2 = usj reboot, 3 = reboot */
static uint8_t s_kbd_leds;

/* ------------------------------------------------------------ log ring */
#define RING_SZ (64 * 1024)
static char *s_ring; /* PSRAM */
static size_t s_ring_head;
static bool s_ring_wrapped;
static portMUX_TYPE s_ring_mux = portMUX_INITIALIZER_UNLOCKED;

void usb_composite_log_tee(const char *s)
{
    if (!s || !s_ring) return;
    taskENTER_CRITICAL(&s_ring_mux);
    for (; *s; s++) {
        s_ring[s_ring_head++] = *s;
        if (s_ring_head == RING_SZ) { s_ring_head = 0; s_ring_wrapped = true; }
    }
    taskEXIT_CRITICAL(&s_ring_mux);
}

static vprintf_like_t s_prev_vprintf;
static int tee_vprintf(const char *fmt, va_list ap)
{
    char buf[256];
    va_list ap2;
    va_copy(ap2, ap);
    vsnprintf(buf, sizeof(buf), fmt, ap2);
    va_end(ap2);
    usb_composite_log_tee(buf);
    return s_prev_vprintf ? s_prev_vprintf(fmt, ap) : vprintf(fmt, ap);
}

/* ------------------------------------------------------------ CDC helpers */
static void cdc_write(const char *s, size_t n)
{
    if (!s_mounted) return;
    int64_t deadline = esp_timer_get_time() + 2000000;
    while (n && esp_timer_get_time() < deadline) {
        size_t w = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)s, n);
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(50));
        if (w == 0) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        s += w; n -= w;
    }
}

static void cdc_printf(const char *fmt, ...)
{
    char buf[384];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) cdc_write(buf, n < (int)sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

static void cdc_dump_log(void)
{
    /* copy under lock, then send */
    size_t len;
    if (!s_ring) return;
    char *tmp = heap_caps_malloc(RING_SZ, MALLOC_CAP_SPIRAM);
    if (!tmp) return;
    taskENTER_CRITICAL(&s_ring_mux);
    if (s_ring_wrapped) {
        size_t a = RING_SZ - s_ring_head;
        memcpy(tmp, s_ring + s_ring_head, a);
        memcpy(tmp + a, s_ring, s_ring_head);
        len = RING_SZ;
    } else {
        memcpy(tmp, s_ring, s_ring_head);
        len = s_ring_head;
    }
    taskEXIT_CRITICAL(&s_ring_mux);
    cdc_printf("---- log (%u bytes) ----\r\n", (unsigned)len);
    for (size_t off = 0; off < len; off += 256) cdc_write(tmp + off, len - off > 256 ? 256 : len - off);
    cdc_printf("\r\n---- end log ----\r\n");
    free(tmp);
}

static void hid_tap_shift(void)
{
    /* Harmless key: left Shift press + release (types nothing). */
    uint8_t none[6] = {0};
    tud_hid_keyboard_report(0, KEYBOARD_MODIFIER_LEFTSHIFT, none);
    vTaskDelay(pdMS_TO_TICKS(30));
    tud_hid_keyboard_report(0, 0, none);
}

static void cmd_status(void)
{
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t we = esp_wifi_get_mode(&mode);
    cdc_printf("reset_reason=%d crash_count=%lu\r\n", (int)esp_reset_reason(), (unsigned long)s_crash_count);
    cdc_printf("fw=%s uptime=%llds mounted=%d suspended=%d remote_wakeup_enabled=%d kbd_leds=0x%02x\r\n",
               USB_FW_TAG, esp_timer_get_time() / 1000000, s_mounted, s_suspended, s_rwu_enabled, s_kbd_leds);
    cdc_printf("heap internal free=%u min=%u, psram free=%u\r\n",
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    cdc_printf("wifi: get_mode=%s mode=%d ; bt controller status=%d (0=idle 1=inited 2=enabled)\r\n",
               esp_err_to_name(we), (int)mode, (int)esp_bt_controller_get_status());
}

static void handle_line(char *line)
{
    if (line[0] == 0x01) {
        cdc_printf("err: too_long (line discarded)\r\n");
        return;
    }
    s_cmd_seen = true;
    if (line[0] != '!') {
        cdc_printf("echo: %s\r\n", line);
        return;
    }
    if (!strcmp(line, "!help")) {
        cdc_printf("cmds: !status !log !key !wake !dfu !usj !reboot ; other lines are echoed\r\n");
    } else if (!strcmp(line, "!status")) {
        cmd_status();
    } else if (!strcmp(line, "!log")) {
        cdc_dump_log();
    } else if (!strcmp(line, "!key")) {
        if (s_suspended) {
            cdc_printf("bus suspended, use !wake\r\n");
        } else if (tud_hid_ready()) {
            hid_tap_shift();
            cdc_printf("ok: shift tapped\r\n");
        } else {
            cdc_printf("hid not ready\r\n");
        }
    } else if (!strcmp(line, "!wake")) {
        esp_err_t e = tinyusb_remote_wakeup();
        cdc_printf("remote_wakeup: %s\r\n", esp_err_to_name(e));
    } else if (!strcmp(line, "!dfu")) {
        cdc_printf("ok: entering ROM download mode on USB-Serial/JTAG\r\n");
        s_pending_action = 1;
    } else if (!strcmp(line, "!usj")) {
        cdc_printf("ok: rebooting once without TinyUSB (USB-Serial/JTAG)\r\n");
        s_pending_action = 2;
    } else if (!strcmp(line, "!reboot")) {
        cdc_printf("ok: reboot\r\n");
        s_pending_action = 3;
    } else {
        cdc_printf("err: unknown command %s\r\n", line);
    }
}

static char s_line[520];
static size_t s_line_len;
static bool s_line_bad;
static QueueHandle_t s_cmd_q; /* lines are handled outside the TinyUSB task (CDC flush would deadlock) */
#define CMD_LINE_MAX 256

static void cdc_rx_cb(int itf, cdcacm_event_t *event)
{
    uint8_t buf[64];
    size_t n = 0;
    while (tinyusb_cdcacm_read(itf, buf, sizeof(buf), &n) == ESP_OK && n > 0) {
        for (size_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\r') continue;
            if (c == '\n') {
                s_line[s_line_len] = 0;
                char item[CMD_LINE_MAX];
                if (s_line_bad) {
                    /* over-long or binary garbage (e.g. esptool SLIP sync on this port) */
                    strlcpy(item, "\x01too_long", sizeof(item));
                    xQueueSend(s_cmd_q, item, 0);
                } else if (s_line_len) {
                    strlcpy(item, s_line, sizeof(item));
                    if (xQueueSend(s_cmd_q, item, 0) != pdTRUE) ESP_LOGW(TAG, "cmd queue full");
                }
                s_line_len = 0;
                s_line_bad = false;
            } else if (c == 0 || s_line_len >= CMD_LINE_MAX - 1) {
                s_line_bad = true;
            } else {
                s_line[s_line_len++] = c;
            }
        }
    }
}

static void cdc_line_coding_cb(int itf, cdcacm_event_t *event)
{
    s_bitrate = event->line_coding_changed_data.p_line_coding->bit_rate;
    ESP_LOGI(TAG, "CDC line coding: %lu bps", (unsigned long)s_bitrate);
}

static void cdc_line_state_cb(int itf, cdcacm_event_t *event)
{
    bool dtr = event->line_state_changed_data.dtr;
    bool rts = event->line_state_changed_data.rts;
    ESP_LOGI(TAG, "CDC line state: DTR=%d RTS=%d (bitrate %lu)", dtr, rts, (unsigned long)s_bitrate);
    if (dtr) { /* new host session: drop any partial line */
        s_line_len = 0;
        s_line_bad = false;
    }
    if (!dtr && s_bitrate == 1200) {
        ESP_LOGW(TAG, "1200-baud touch detected -> download mode");
        s_pending_action = 1;
    }
}

/* ------------------------------------------------------------ HID callbacks */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return s_hid_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= 1) {
        s_kbd_leds = buffer[0];
    }
}

/* ------------------------------------------------------------ events */
static void usb_event_cb(tinyusb_event_t *ev, void *arg)
{
    switch (ev->id) {
    case TINYUSB_EVENT_ATTACHED:
        s_mounted = true;
        s_suspended = false;
        ESP_LOGI(TAG, "USB mounted by host");
        break;
    case TINYUSB_EVENT_DETACHED:
        s_mounted = false;
        ESP_LOGI(TAG, "USB detached");
        break;
#ifdef CONFIG_TINYUSB_SUSPEND_CALLBACK
    case TINYUSB_EVENT_SUSPENDED:
        s_suspended = true;
        s_rwu_enabled = ev->suspended.remote_wakeup;
        ESP_LOGI(TAG, "USB suspended (remote wakeup %s by host)", s_rwu_enabled ? "ENABLED" : "disabled");
        break;
#endif
#ifdef CONFIG_TINYUSB_RESUME_CALLBACK
    case TINYUSB_EVENT_RESUMED:
        s_suspended = false;
        ESP_LOGI(TAG, "USB resumed");
        break;
#endif
    default:
        break;
    }
}

/* ------------------------------------------------------------ reboot paths */
static void phy_to_usj(void)
{
    /* Route the internal FS PHY back to USB-Serial/JTAG (RTC register, survives SW reset). */
    usb_serial_jtag_ll_phy_enable_external(false);
}

static void do_download_mode(void)
{
    ESP_LOGW(TAG, "-> ROM download mode via USB-Serial/JTAG");
    vTaskDelay(pdMS_TO_TICKS(150)); /* let the CDC reply flush */
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    phy_to_usj();
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}

static void do_usj_reboot(void)
{
    ESP_LOGW(TAG, "-> reboot once without TinyUSB");
    vTaskDelay(pdMS_TO_TICKS(150));
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    phy_to_usj();
    s_skip_usb_magic = SKIP_USB_MAGIC;
    esp_restart();
}

static void cmd_task(void *arg)
{
    char item[CMD_LINE_MAX];
    while (1) {
        if (xQueueReceive(s_cmd_q, item, portMAX_DELAY) == pdTRUE) handle_line(item);
    }
}

static void supervisor_task(void *arg)
{
    int64_t t0 = esp_timer_get_time();
    int btn_ms = 0;
    gpio_config_t b = {.pin_bit_mask = BIT64(0), .mode = GPIO_MODE_INPUT, .pull_up_en = 1};
    gpio_config(&b);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));
        int64_t up_s = (esp_timer_get_time() - t0) / 1000000;
        /* BOOT button held >= 2 s at runtime -> download mode (no replug needed) */
        btn_ms = gpio_get_level(0) == 0 ? btn_ms + 50 : 0;
        if (btn_ms >= 2000) s_pending_action = 1;

        if (!s_mounted && USB_MOUNT_TIMEOUT_S > 0 && up_s >= USB_MOUNT_TIMEOUT_S) {
            static bool once;
            if (!once) {
                once = true;
                ESP_LOGE(TAG, "not mounted after %ds -> safety fallback to USJ", USB_MOUNT_TIMEOUT_S);
                s_pending_action = 2;
            }
        }
        if (!s_cmd_seen && USB_NO_CMD_TIMEOUT_S > 0 && up_s >= USB_NO_CMD_TIMEOUT_S) {
            ESP_LOGE(TAG, "no CDC command after %ds -> safety fallback to USJ", USB_NO_CMD_TIMEOUT_S);
            s_pending_action = 2;
        }
        switch (s_pending_action) {
        case 1: do_download_mode(); break;
        case 2: do_usj_reboot(); break;
        case 3: vTaskDelay(pdMS_TO_TICKS(150)); esp_restart(); break;
        default: break;
        }
    }
}

int usb_composite_start(void)
{
    static bool started;
    if (started) return 0;
    started = true;

    s_ring = heap_caps_malloc(RING_SZ, MALLOC_CAP_SPIRAM);
    s_prev_vprintf = esp_log_set_vprintf(tee_vprintf);

    esp_reset_reason_t rr = esp_reset_reason();
    if (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT) {
        s_crash_count = (s_crash_count < 100) ? s_crash_count + 1 : 100;
    } else {
        s_crash_count = 0;
    }
    if (s_crash_count >= 3) {
        s_crash_count = 0;
        RTCCNTL.usb_conf.sw_hw_usb_phy_sel = 0;
        ESP_LOGE(TAG, "3 consecutive crash resets -> TinyUSB skipped (USJ stays available for esptool)");
        return 2;
    }
    if (s_skip_usb_magic == SKIP_USB_MAGIC) {
        s_skip_usb_magic = 0; /* only for this boot */
        /* Give PHY muxing back to hardware/eFuse control so the USJ DTR/RTS
         * "reset into bootloader" sequence used by esptool works again. */
        RTCCNTL.usb_conf.sw_hw_usb_phy_sel = 0;
        ESP_LOGW(TAG, "TinyUSB skipped for this boot (USJ mode requested). Next reset re-enables it.");
        return 1;
    }

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_serial, sizeof(s_serial), "MESP-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);

    s_cmd_q = xQueueCreate(4, CMD_LINE_MAX);
    xTaskCreate(cmd_task, "usb_cmd", 6144, NULL, 5, NULL);
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG(usb_event_cb);
    cfg.descriptor.device = &s_dev_desc;
    cfg.descriptor.string = s_strings;
    cfg.descriptor.string_count = sizeof(s_strings) / sizeof(s_strings[0]);
    cfg.descriptor.full_speed_config = s_cfg_desc;
    ESP_LOGI(TAG, "starting TinyUSB composite HID+CDC, serial %s (USJ console will drop now)", s_serial);
    esp_err_t err = tinyusb_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install: %s", esp_err_to_name(err));
        return -1;
    }
    tinyusb_config_cdcacm_t acm = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = cdc_rx_cb,
        .callback_line_state_changed = cdc_line_state_cb,
        .callback_line_coding_changed = cdc_line_coding_cb,
    };
    err = tinyusb_cdcacm_init(&acm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_cdcacm_init: %s", esp_err_to_name(err));
    }
    xTaskCreate(supervisor_task, "usb_sup", 4096, NULL, 6, NULL);
    return 0;
}
