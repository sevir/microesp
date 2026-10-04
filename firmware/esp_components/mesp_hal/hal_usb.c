/*
 * MicroESP HAL — composite USB device on the ESP32-S3 USB-OTG (TinyUSB via esp_tinyusb):
 *   - HID boot keyboard, configuration bmAttributes REMOTE_WAKEUP
 *   - CDC ACM: cdc-v1 protocol lines + "!" CLI lines (one line <= 511 bytes + '\n')
 *   - VID 0x303A / PID 0x4002, product "MicroESP", serial "MESP-<wifi mac>"
 *
 * Safety nets (from the MESP-US-0003 spike, extended):
 *   - "!dfu" or 1200-baud touch: PHY back to USB-Serial/JTAG + FORCE_DOWNLOAD_BOOT ->
 *     ROM download mode (303a:1001); flash with esptool's DEFAULT reset sequence.
 *   - BOOT button held >= 20 s (after being seen released once): same as !dfu.
 *   - "!usj": reboot once without TinyUSB (USJ console, normal esptool).
 *   - Not enumerated within 20 s -> reboot once without TinyUSB. If no USB host is seen
 *     on USB-Serial/JTAG within 60 s of that boot (e.g. the PC is off: S5 with
 *     always-on USB), reboot back into TinyUSB with the 20 s fallback inhibited, so HID
 *     wake keeps working while the PC is off.
 *   - 3 crash resets in a row -> TinyUSB skipped for that boot.
 *   - "!log": dump the 64 KB RAM log ring (PSRAM).
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "mesp_board.h"
#include "mesp_hal.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_cntl_struct.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

#define USB_MOUNT_TIMEOUT_S   20
#define USJ_NO_HOST_RETURN_S  60
#define BUTTON_DFU_MS         20000
#define CDC_LINE_MAX              512 /* incl. terminator */
#define CMD_Q_LEN             6

extern void dcd_remote_wakeup(uint8_t rhport); /* TinyUSB DCD (dwc2) */

static const char *TAG = "mhal_usb";

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
RTC_NOINIT_ATTR static uint32_t s_skip_reason;
RTC_NOINIT_ATTR static uint32_t s_inhibit_magic;
RTC_NOINIT_ATTR static uint32_t s_crash_count;
#define SKIP_USB_MAGIC 0x55534A31 /* "USJ1" */
#define INHIBIT_MAGIC  0x4E4F4D31 /* "NOM1" */

static volatile bool s_mounted, s_suspended, s_rwu_enabled, s_driver_ok;
static volatile uint32_t s_bitrate = 115200;
static volatile int s_pending_action;
static volatile uint8_t s_kbd_leds;
static volatile uint8_t s_hid_protocol = HID_PROTOCOL_REPORT;
static int s_mode = -1;
static bool s_mount_fallback_inhibited;
static mhal_usb_cbs_t s_cbs;
static SemaphoreHandle_t s_tx_mutex;

void mhal_usb_set_callbacks(const mhal_usb_cbs_t *cbs)
{
    if (cbs) s_cbs = *cbs;
}

int mhal_usb_mode(void) { return s_mode; }
bool mhal_usb_mounted(void) { return s_mounted; }
bool mhal_usb_suspended(void) { return s_suspended; }
bool mhal_usb_rwu_enabled(void) { return s_rwu_enabled; }
uint8_t mhal_kbd_leds(void) { return s_kbd_leds; }
uint8_t mhal_hid_protocol(void) { return s_hid_protocol; }
const char *mhal_usb_serial(void) { return s_serial; }
uint32_t mhal_crash_count(void) { return s_crash_count; }
void mhal_request(int action) { s_pending_action = action; }

/* 32-bit milliseconds: read/written atomically (an int64 could tear on this CPU) */
static volatile uint32_t s_app_alive_ms;
static volatile bool s_app_alive_started;
void mhal_app_alive(void)
{
    s_app_alive_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_app_alive_started = true;
}

bool mhal_cdc_connected(void) { return s_driver_ok && s_mounted && tud_cdc_n_connected(0); }

/* ------------------------------------------------------------ log ring */
#define RING_SZ (64 * 1024)
static char *s_ring; /* PSRAM */
static size_t s_ring_head;
static bool s_ring_wrapped;
static portMUX_TYPE s_ring_mux = portMUX_INITIALIZER_UNLOCKED;

void mhal_log_tee(const char *s)
{
    if (!s || !s_ring) return;
    taskENTER_CRITICAL(&s_ring_mux);
    for (; *s; s++) {
        s_ring[s_ring_head++] = *s;
        if (s_ring_head == RING_SZ) {
            s_ring_head = 0;
            s_ring_wrapped = true;
        }
    }
    taskEXIT_CRITICAL(&s_ring_mux);
}

static vprintf_like_t s_prev_vprintf;
static const char *(*volatile s_log_filter)(const char *);

void mhal_log_set_filter(const char *(*filter)(const char *line)) { s_log_filter = filter; }

static int raw_out(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = s_prev_vprintf ? s_prev_vprintf(fmt, ap) : vprintf(fmt, ap);
    va_end(ap);
    return n;
}

/* ESP-IDF log lines (Wi-Fi, esp-mqtt, TLS...): redacted like the TuyaOpen/app ones
 * (the filter matches registered secrets and sensitive keywords). A line whose text
 * is longer than the buffer is only checked on its first 255 bytes (the buffer lives
 * on the stack of whatever task logs, e.g. sys_evt). */
static int tee_vprintf(const char *fmt, va_list ap)
{
    char buf[256];
    va_list ap2;
    va_copy(ap2, ap);
    vsnprintf(buf, sizeof(buf), fmt, ap2);
    va_end(ap2);
    const char *(*f)(const char *) = s_log_filter;
    const char *out = f ? f(buf) : buf;
    if (out != buf) {
        mhal_log_tee(out);
        return raw_out("%s", out);
    }
    mhal_log_tee(buf);
    return s_prev_vprintf ? s_prev_vprintf(fmt, ap) : vprintf(fmt, ap);
}

void mhal_log_init(void)
{
    if (s_ring) return;
    s_ring = heap_caps_malloc(RING_SZ, MALLOC_CAP_SPIRAM);
    s_prev_vprintf = esp_log_set_vprintf(tee_vprintf);
}

/* ------------------------------------------------------------ CDC output */
int mhal_cdc_write(const char *s, size_t n)
{
    if (!mhal_cdc_connected() || !s_tx_mutex) return -1;
    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return -1;
    int64_t deadline = esp_timer_get_time() + 500000;
    size_t left = n;
    while (left && esp_timer_get_time() < deadline && mhal_cdc_connected()) {
        size_t w = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)s, left);
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(50));
        if (w == 0) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }
        s += w;
        left -= w;
    }
    xSemaphoreGive(s_tx_mutex);
    return left ? -1 : (int)n;
}

int mhal_cdc_printf(const char *fmt, ...)
{
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return n;
    return mhal_cdc_write(buf, n < (int)sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

static void cdc_dump_log(void)
{
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
    mhal_cdc_printf("---- log (%u bytes) ----\r\n", (unsigned)len);
    for (size_t off = 0; off < len; off += 256) mhal_cdc_write(tmp + off, len - off > 256 ? 256 : len - off);
    mhal_cdc_printf("\r\n---- end log ----\r\n");
    free(tmp);
}

/* ------------------------------------------------------------ HID */
int mhal_hid_remote_wakeup(void)
{
    if (!s_driver_ok) return -1;
    return tinyusb_remote_wakeup() == ESP_OK ? 0 : -1;
}

int mhal_hid_force_resume(void)
{
    if (!s_driver_ok) return -1;
    ESP_LOGW(TAG, "forced resume signalling (mounted=%d suspended=%d armed=%d)", s_mounted, s_suspended,
             s_rwu_enabled);
    dcd_remote_wakeup(0);
    return 0;
}

static bool hid_wait_ready(int ms)
{
    for (; ms > 0; ms -= 5) {
        if (s_mounted && !s_suspended && tud_hid_ready()) return true;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false;
}

int mhal_hid_tap(uint8_t modifier, uint8_t keycode)
{
    if (!s_driver_ok || !s_mounted || s_suspended || !hid_wait_ready(100)) return -1;
    uint8_t keys[6] = {keycode, 0, 0, 0, 0, 0};
    if (!tud_hid_keyboard_report(0, modifier, keys)) return -1;
    vTaskDelay(pdMS_TO_TICKS(50));
    /* The release must not be dropped (stuck key): wait until the host polled the
     * press report (a BIOS/EC host may poll slowly), then retry the release. */
    uint8_t none[6] = {0};
    for (int i = 0; i < 3; i++) {
        if (hid_wait_ready(200) && tud_hid_keyboard_report(0, 0, none)) return 0;
    }
    ESP_LOGW(TAG, "HID key release not sent (bus went away?)");
    return -1;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) { return s_hid_report_desc; }

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen)
{
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize)
{
    if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= 1) s_kbd_leds = buffer[0];
}

/* Diagnostic: a BIOS/EC host (e.g. Lenovo Smart Power On in S5) usually selects the boot
 * protocol (0); an OS driver normally leaves the report protocol (1). */
void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol)
{
    s_hid_protocol = protocol;
    ESP_LOGI(TAG, "HID SET_PROTOCOL %s", protocol == HID_PROTOCOL_BOOT ? "boot" : "report");
}

/* ------------------------------------------------------------ CDC input */
static char s_line[CDC_LINE_MAX];
static size_t s_line_len;
static bool s_line_bad;
static QueueHandle_t s_cmd_q; /* lines are handled outside the TinyUSB task (CDC writes would deadlock) */
#define TOO_LONG_MARK 0x01

static void cdc_rx_cb(int itf, cdcacm_event_t *event)
{
    static char item[CDC_LINE_MAX];
    uint8_t buf[64];
    size_t n = 0;
    while (tinyusb_cdcacm_read(itf, buf, sizeof(buf), &n) == ESP_OK && n > 0) {
        for (size_t i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n') {
                if (s_line_bad) {
                    item[0] = TOO_LONG_MARK;
                    item[1] = 0;
                    xQueueSend(s_cmd_q, item, 0);
                } else {
                    /* strip a trailing \r */
                    if (s_line_len && s_line[s_line_len - 1] == '\r') s_line_len--;
                    if (s_line_len) {
                        memcpy(item, s_line, s_line_len);
                        item[s_line_len] = 0;
                        if (xQueueSend(s_cmd_q, item, 0) != pdTRUE) ESP_LOGW(TAG, "cmd queue full, line dropped");
                    }
                }
                s_line_len = 0;
                s_line_bad = false;
            } else if (s_line_bad) {
                /* discard until the next '\n' */
            } else if (s_line_len >= CDC_LINE_MAX - 1) {
                s_line_bad = true;
            } else {
                s_line[s_line_len++] = c ? c : '?'; /* NUL would truncate the C string */
            }
        }
    }
}

static void cdc_line_coding_cb(int itf, cdcacm_event_t *event)
{
    s_bitrate = event->line_coding_changed_data.p_line_coding->bit_rate;
}

static void cdc_line_state_cb(int itf, cdcacm_event_t *event)
{
    bool dtr = event->line_state_changed_data.dtr;
    if (dtr) { /* new host session: drop any partial line */
        s_line_len = 0;
        s_line_bad = false;
    }
    if (s_cbs.on_cdc_dtr) s_cbs.on_cdc_dtr(dtr);
    if (!dtr && s_bitrate == 1200) {
        ESP_LOGW(TAG, "1200-baud touch detected -> download mode");
        s_pending_action = MHAL_ACT_DFU;
    }
}

static void handle_line(char *line)
{
    if (line[0] == TOO_LONG_MARK) {
        if (s_cbs.on_too_long) s_cbs.on_too_long();
        else mhal_cdc_printf("{\"t\":\"err\",\"code\":\"too_long\"}\n");
        return;
    }
    if (!strcmp(line, "!dfu")) {
        mhal_cdc_printf("ok: entering ROM download mode on USB-Serial/JTAG\r\n");
        s_pending_action = MHAL_ACT_DFU;
    } else if (!strcmp(line, "!usj")) {
        mhal_cdc_printf("ok: rebooting once without TinyUSB (USB-Serial/JTAG)\r\n");
        s_skip_reason = MHAL_USB_SKIPPED_USJ;
        s_pending_action = MHAL_ACT_USJ;
    } else if (!strcmp(line, "!log")) {
        cdc_dump_log();
    } else if (!strcmp(line, "!reboot")) {
        mhal_cdc_printf("ok: reboot\r\n");
        s_pending_action = MHAL_ACT_REBOOT;
    } else if (s_cbs.on_line) {
        s_cbs.on_line(line, strlen(line));
    } else {
        mhal_cdc_printf("err: application not ready\r\n");
    }
}

/* ------------------------------------------------------------ bus events */
static void usb_event_cb(tinyusb_event_t *ev, void *arg)
{
    switch (ev->id) {
    case TINYUSB_EVENT_ATTACHED:
        s_mounted = true;
        s_suspended = false;
        s_hid_protocol = HID_PROTOCOL_REPORT; /* default after (re)configuration */
        ESP_LOGI(TAG, "USB mounted by host");
        if (s_cbs.on_usb) s_cbs.on_usb(MHAL_USB_MOUNT, false);
        break;
    case TINYUSB_EVENT_DETACHED:
        s_mounted = false;
        s_suspended = false;
        ESP_LOGI(TAG, "USB detached / unconfigured");
        if (s_cbs.on_usb) s_cbs.on_usb(MHAL_USB_UMOUNT, false);
        break;
#ifdef CONFIG_TINYUSB_SUSPEND_CALLBACK
    case TINYUSB_EVENT_SUSPENDED:
        s_suspended = true;
        s_rwu_enabled = ev->suspended.remote_wakeup;
        ESP_LOGI(TAG, "USB suspended (remote wakeup %s by host)", s_rwu_enabled ? "ARMED" : "NOT armed");
        if (s_cbs.on_usb) s_cbs.on_usb(MHAL_USB_SUSPEND, s_rwu_enabled);
        break;
#endif
#ifdef CONFIG_TINYUSB_RESUME_CALLBACK
    case TINYUSB_EVENT_RESUMED:
        s_suspended = false;
        ESP_LOGI(TAG, "USB resumed");
        if (s_cbs.on_usb) s_cbs.on_usb(MHAL_USB_RESUME, s_rwu_enabled);
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
    if (s_driver_ok) {
        tud_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    phy_to_usj();
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}

static void do_usj_reboot(void)
{
    ESP_LOGW(TAG, "-> reboot once without TinyUSB (reason %lu)", (unsigned long)s_skip_reason);
    vTaskDelay(pdMS_TO_TICKS(150));
    if (s_driver_ok) {
        tud_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    phy_to_usj();
    s_skip_usb_magic = SKIP_USB_MAGIC;
    esp_restart();
}

static void cmd_task(void *arg)
{
    static char item[CDC_LINE_MAX];
    while (1) {
        if (xQueueReceive(s_cmd_q, item, portMAX_DELAY) == pdTRUE) handle_line(item);
    }
}

static void supervisor_task(void *arg)
{
    int64_t t0 = esp_timer_get_time();
    int btn_ms = 0;
    bool btn_seen_released = false;
    bool usj_host_seen = false;
    gpio_config_t b = {.pin_bit_mask = BIT64(MESP_PIN_BUTTON), .mode = GPIO_MODE_INPUT, .pull_up_en = 1};
    gpio_config(&b);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));
        int64_t up_s = (esp_timer_get_time() - t0) / 1000000;
        /* BOOT held >= 20 s -> download mode (emergency recovery, no replug needed) */
        bool pressed = gpio_get_level(MESP_PIN_BUTTON) == 0;
        if (!pressed) btn_seen_released = true;
        btn_ms = (pressed && btn_seen_released) ? btn_ms + 50 : 0;
        if (btn_ms >= BUTTON_DFU_MS) s_pending_action = MHAL_ACT_DFU;

        /* software watchdog of the application task (hung lock, blocked call) */
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (s_app_alive_started && (int32_t)(now_ms - s_app_alive_ms) > MHAL_APP_WDT_S * 1000) {
            static bool wdt_once;
            if (!wdt_once) {
                wdt_once = true;
                ESP_LOGE(TAG, "app task stalled for > %ds -> restart", MHAL_APP_WDT_S);
                s_pending_action = MHAL_ACT_REBOOT;
            }
        }

        if (s_mode == MHAL_USB_OK) {
            if (s_mounted) s_inhibit_magic = 0; /* a host enumerated us: re-arm the fallback for next boots */
            if (!s_mounted && !s_mount_fallback_inhibited && up_s >= USB_MOUNT_TIMEOUT_S) {
                static bool once;
                if (!once) {
                    once = true;
                    ESP_LOGE(TAG, "not mounted after %ds -> safety fallback to USJ", USB_MOUNT_TIMEOUT_S);
                    s_skip_reason = MHAL_USB_SKIPPED_NOMOUNT;
                    s_pending_action = MHAL_ACT_USJ;
                }
            }
        } else if (s_mode == MHAL_USB_SKIPPED_NOMOUNT) {
            /* USJ fallback boot: is there a USB host at all? (SOF every 1 ms) */
            if (usb_serial_jtag_ll_get_intraw_mask() & USB_SERIAL_JTAG_INTR_SOF) {
                usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SOF);
                if (!usj_host_seen) ESP_LOGW(TAG, "USB host present on USB-Serial/JTAG: staying in USJ mode");
                usj_host_seen = true;
            }
            if (!usj_host_seen && up_s >= USJ_NO_HOST_RETURN_S) {
                ESP_LOGW(TAG, "no USB host for %ds -> back to TinyUSB (mount fallback inhibited)",
                         USJ_NO_HOST_RETURN_S);
                s_inhibit_magic = INHIBIT_MAGIC;
                s_pending_action = MHAL_ACT_REBOOT;
            }
        }
        switch (s_pending_action) {
        case MHAL_ACT_DFU: do_download_mode(); break;
        case MHAL_ACT_USJ: do_usj_reboot(); break;
        case MHAL_ACT_REBOOT:
            vTaskDelay(pdMS_TO_TICKS(150));
            esp_restart();
            break;
        default: break;
        }
    }
}

int mhal_usb_start(void)
{
    if (s_mode >= 0) return s_mode;
    s_tx_mutex = xSemaphoreCreateMutex();
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_serial, sizeof(s_serial), "MESP-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);

    esp_reset_reason_t rr = esp_reset_reason();
    if (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT) {
        s_crash_count = (s_crash_count < 100) ? s_crash_count + 1 : 100;
    } else {
        s_crash_count = 0;
    }
    if (rr == ESP_RST_POWERON) {
        s_skip_usb_magic = 0;
        s_inhibit_magic = 0;
    }
    s_mount_fallback_inhibited = (s_inhibit_magic == INHIBIT_MAGIC);
    if (s_crash_count >= 3) {
        s_crash_count = 0;
        RTCCNTL.usb_conf.sw_hw_usb_phy_sel = 0;
        ESP_LOGE(TAG, "3 consecutive crash resets -> TinyUSB skipped (USJ stays available for esptool)");
        s_mode = MHAL_USB_SKIPPED_CRASH;
    } else if (s_skip_usb_magic == SKIP_USB_MAGIC) {
        s_skip_usb_magic = 0; /* only for this boot */
        /* Give PHY muxing back to hardware/eFuse control so the USJ DTR/RTS
         * "reset into bootloader" sequence used by esptool works again. */
        RTCCNTL.usb_conf.sw_hw_usb_phy_sel = 0;
        s_mode = s_skip_reason == MHAL_USB_SKIPPED_NOMOUNT ? MHAL_USB_SKIPPED_NOMOUNT : MHAL_USB_SKIPPED_USJ;
        usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SOF);
        ESP_LOGW(TAG, "TinyUSB skipped for this boot (USJ mode, reason %d). Next reset re-enables it.", s_mode);
    }
    if (s_mode >= 0) {
        xTaskCreate(supervisor_task, "usb_sup", 3072, NULL, 6, NULL);
        return s_mode;
    }

    s_cmd_q = xQueueCreate(CMD_Q_LEN, CDC_LINE_MAX);
    xTaskCreate(cmd_task, "usb_cmd", 6144, NULL, 5, NULL);
    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG(usb_event_cb);
    cfg.descriptor.device = &s_dev_desc;
    cfg.descriptor.string = s_strings;
    cfg.descriptor.string_count = sizeof(s_strings) / sizeof(s_strings[0]);
    cfg.descriptor.full_speed_config = s_cfg_desc;
    ESP_LOGI(TAG, "starting TinyUSB composite HID+CDC, serial %s (USJ console drops now)%s", s_serial,
             s_mount_fallback_inhibited ? " [mount fallback inhibited]" : "");
    esp_err_t err = tinyusb_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install: %s", esp_err_to_name(err));
        s_mode = MHAL_USB_OK; /* supervisor still provides the mount fallback */
        xTaskCreate(supervisor_task, "usb_sup", 3072, NULL, 6, NULL);
        return -1;
    }
    tinyusb_config_cdcacm_t acm = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = cdc_rx_cb,
        .callback_line_state_changed = cdc_line_state_cb,
        .callback_line_coding_changed = cdc_line_coding_cb,
    };
    err = tinyusb_cdcacm_init(&acm);
    if (err != ESP_OK) ESP_LOGE(TAG, "tinyusb_cdcacm_init: %s", esp_err_to_name(err));
    s_driver_ok = true;
    s_mode = MHAL_USB_OK;
    xTaskCreate(supervisor_task, "usb_sup", 3072, NULL, 6, NULL);
    return s_mode;
}
