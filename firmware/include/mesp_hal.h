/*
 * MicroESP — hardware abstraction between the TuyaOpen application (src/, which cannot
 * see ESP-IDF headers) and the ESP-IDF component esp_components/mesp_hal.
 * Only standard C types here.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ USB (TinyUSB HID + CDC) */
typedef enum { MHAL_USB_MOUNT = 0, MHAL_USB_UMOUNT, MHAL_USB_SUSPEND, MHAL_USB_RESUME } mhal_usb_evt_t;

typedef struct {
    /* bus events (TinyUSB task context: must not block) */
    void (*on_usb)(mhal_usb_evt_t ev, bool remote_wakeup_en);
    /* complete CDC line without terminator (<= 511 bytes), not one of the HAL's own
     * commands (!dfu !usj !log !reboot). Called from the HAL command task. */
    void (*on_line)(const char *line, size_t len);
    /* a line longer than 511 bytes was discarded */
    void (*on_too_long)(void);
    /* host program opened (DTR=1) / closed (DTR=0) the CDC port */
    void (*on_cdc_dtr)(bool dtr);
} mhal_usb_cbs_t;

/* Safety-net results of mhal_usb_start() */
#define MHAL_USB_OK             0
#define MHAL_USB_SKIPPED_USJ    1 /* "!usj" requested: this boot runs without TinyUSB */
#define MHAL_USB_SKIPPED_CRASH  2 /* 3 crash resets in a row */
#define MHAL_USB_SKIPPED_NOMOUNT 3 /* previous boot was not enumerated within 20 s */

void mhal_usb_set_callbacks(const mhal_usb_cbs_t *cbs);
int mhal_usb_start(void);
int mhal_usb_mode(void); /* result of mhal_usb_start */
bool mhal_usb_mounted(void);
bool mhal_usb_suspended(void);
bool mhal_usb_rwu_enabled(void); /* host armed remote wakeup at the last suspend */
bool mhal_cdc_connected(void);   /* DTR asserted by a host program */
uint8_t mhal_kbd_leds(void);
/* Thread-safe; drops output when no host program has the port open. */
int mhal_cdc_write(const char *s, size_t n);
int mhal_cdc_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Standard remote wakeup (bus suspended + armed). 0 = signalled. */
int mhal_hid_remote_wakeup(void);
/* Raw resume (K-state) signalling regardless of state (best effort). 0 = signalled. */
int mhal_hid_force_resume(void);
/* Harmless key tap while the bus is up (modifier mask / usage code). 0 = sent. */
int mhal_hid_tap(uint8_t modifier, uint8_t keycode);
const char *mhal_usb_serial(void); /* "MESP-<mac12>" */

/* Reboot paths (executed by the HAL supervisor task) */
#define MHAL_ACT_DFU    1 /* ROM download mode on USB-Serial/JTAG */
#define MHAL_ACT_USJ    2 /* reboot once without TinyUSB */
#define MHAL_ACT_REBOOT 3
void mhal_request(int action);
/* Software watchdog: the app task calls this every loop iteration. Once called, the
 * HAL supervisor restarts the chip if it is not called again for MHAL_APP_WDT_S. */
#define MHAL_APP_WDT_S 60
void mhal_app_alive(void);

/* RAM log ring (dumped by "!log") */
void mhal_log_tee(const char *s);
void mhal_log_init(void);

/* ------------------------------------------------------------------ system */
void mhal_random(void *buf, size_t n);
void mhal_mac_hex(char out[13]); /* Wi-Fi STA MAC, lowercase */
uint32_t mhal_uptime_s(void);
uint32_t mhal_heap_internal_free(void);
uint32_t mhal_heap_internal_min(void);
uint32_t mhal_psram_free(void);
const char *mhal_reset_reason(void);
uint32_t mhal_crash_count(void);

/* ------------------------------------------------------------------ NVS (namespace "microesp") */
int mhal_nvs_init(void);
int mhal_nvs_get_blob(const char *key, void *buf, size_t *len); /* 0 = ok */
int mhal_nvs_set_blob(const char *key, const void *buf, size_t len);
int mhal_nvs_get_str(const char *key, char *buf, size_t len);
int mhal_nvs_set_str(const char *key, const char *val);
int mhal_nvs_get_u8(const char *key, uint8_t *v);
int mhal_nvs_set_u8(const char *key, uint8_t v);
int mhal_nvs_erase(const char *key);

/* ------------------------------------------------------------------ OTA / rollback */
#define MHAL_OTA_NA      0 /* no OTA state (serial flash) or already valid */
#define MHAL_OTA_PENDING 1 /* new image waiting for the health check */
int mhal_ota_state(void);
const char *mhal_ota_running(void); /* partition label */
bool mhal_ota_rollback_enabled(void);
int mhal_ota_mark_valid(void);

/* ------------------------------------------------------------------ button / LED / LCD */
void mhal_button_init(void);
bool mhal_button_pressed(void);

int mhal_led_init(void);
void mhal_led_set(uint8_t r, uint8_t g, uint8_t b);

int mhal_lcd_init(void);
/* DMA-capable internal buffer for LVGL partial rendering */
void *mhal_lcd_buf(size_t *bytes);
/* Draw RGB565 (big-endian, already swapped) pixels; blocks until the DMA is done. */
int mhal_lcd_draw(int x1, int y1, int x2, int y2, const void *px);
void mhal_lcd_backlight(bool on);

/* ------------------------------------------------------------------ network */
/* Broadcast a WOL packet (UDP 9 and 7, 255.255.255.255 + subnet broadcast). 0 = sent. */
int mhal_wol_send(const uint8_t *pkt, size_t len);
/* Station IPv4 as text, false if none */
bool mhal_ip(char out[16]);

#ifdef __cplusplus
}
#endif
