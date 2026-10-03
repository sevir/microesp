/* MicroESP firmware — module interfaces (all called from the app task unless noted). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app.h"

/* usb_composite.c — HID+CDC device events -> app */
void usbc_init(void); /* boot thread, very early: starts TinyUSB + safety nets */
void usbc_on_event(const app_ev_t *ev);
bool usbc_mounted(void);
bool usbc_suspended(void);
bool usbc_rwu(void);

/* agent_link.c — cdc-v1 session glue */
void agent_link_init(void);
void agent_link_on_line(const char *line, size_t len, uint32_t now);
void agent_link_on_too_long(uint32_t now);
void agent_link_tick(uint32_t now);
void agent_link_on_port_closed(void);

/* pairing.c — agent pairing mode (6-digit code, 120 s) */
void pairing_init(void);
void pairing_start(const char *why);
void pairing_stop(void);
void pairing_forget(void);
bool pairing_active(void);
const char *pairing_code(void);
int pairing_remaining_s(void);
void pairing_on_end(void);

/* wake.c — power-on via HID / WOL */
void wake_mod_init(void);
wake_rc_t wake_power_on(const char *source);
void wake_mod_tick(uint32_t now);
void wake_set_method(wake_method_t m);
void wake_store_macs(const uint8_t macs[][6], int n);

/* state.c — PC state + fault bitmap */
void state_init(void);
void state_tick(uint32_t now);

/* power.c — shutdown/reboot countdown + cmd */
void power_init(void);
void power_request(pwr_action_t a, const char *source);
bool power_cancel(const char *source);
void power_tick(uint32_t now);
void power_set_countdown(int s);

/* cloud.c — TuyaLink client glue (Wi-Fi + MQTT in mesp_hal/hal_cloud.c) */
void cloud_init(void); /* boot thread, before the app task starts */
void cloud_on_event(const app_ev_t *ev); /* EV_CLOUD */
void cloud_on_rx(const app_ev_t *ev);    /* EV_CLOUD_RX */
void cloud_tick(uint32_t now);
void cloud_print_status(void);
bool cloud_tylink_provisioned(void); /* NVS, current */
bool cloud_wifi_provisioned(void);
int cloud_set_tylink(const char *region, const char *pid, const char *did, const char *secret); /* 0 / -1 invalid / -2 NVS */
int cloud_set_wifi(const char *ssid, const char *pass);
int cloud_clear_tylink(void); /* dev CLI "!tylink clear" */
int cloud_clear_wifi(void);   /* dev CLI "!wifi clear" */

/* button.c / led.c / display.c */
void button_init(void);
void button_tick(uint32_t now);
void led_init(void);
void led_tick(uint32_t now);
void display_init(void);
void display_tick(uint32_t now);
void display_next_screen(void);

/* ota.c */
void ota_init(void);
void ota_on_event(const app_ev_t *ev);
void ota_tick(uint32_t now);
const char *ota_status(void);

/* cli.c — "!" commands on the CDC port */
void cli_handle(const char *line);
void cli_tick(uint32_t now);
void cli_prov_window_open(void); /* physical gesture (button 5 s): !tylink/!wifi for 120 s */
int cli_prov_window_remaining_s(void);
