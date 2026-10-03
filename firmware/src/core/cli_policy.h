/*
 * MicroESP — CDC CLI access policy (security hardening), pure C and host-tested.
 *
 * Anyone who can open the CDC port (dialout/root on the PC) can type CLI lines, so a
 * RELEASE build (MESP_DEV_CLI=n) only accepts read-only/recovery commands:
 *     !help !status !version !dp !log !dfu !usj !reboot !cancel
 * Cloud provisioning (!tylink, !wifi) is accepted in release only while that item is
 * not provisioned yet, or inside the 120 s provisioning window opened by a physical
 * gesture (button held 5..10 s). Everything else (!pair !unpair !wake !method
 * !countdown !cmd !key) is development-only (MESP_DEV_CLI=y, "./build.sh dev").
 * The TuyaOS-era commands (!auth !pid !reset-tuya) are obsolete with TuyaLink and
 * answer with a clear error in every build.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLIP_PROV_WINDOW_MS 120000

typedef enum {
    CLIP_ALLOW = 0,
    CLIP_DEV_ONLY, /* command exists but needs a development build */
    CLIP_LOCKED,   /* provisioning command outside the provisioning window */
    CLIP_UNKNOWN,
    CLIP_OBSOLETE, /* TuyaOS-era command, not used with TuyaLink */
} clip_rc_t;

typedef struct {
    bool dev;                /* MESP_DEV_CLI build */
    bool tylink_provisioned; /* TuyaLink region/productId/deviceId/deviceSecret in NVS */
    bool wifi_provisioned;   /* Wi-Fi SSID in NVS */
    bool prov_window;        /* physical provisioning window open */
} clip_ctx_t;

clip_rc_t clip_check(const char *cmd, const clip_ctx_t *c);

typedef struct {
    bool open;
    uint32_t until_ms;
} clip_window_t;

void clip_window_open(clip_window_t *w, uint32_t now_ms);
void clip_window_close(clip_window_t *w);
bool clip_window_active(clip_window_t *w, uint32_t now_ms); /* closes it on expiry */
int clip_window_remaining_s(const clip_window_t *w, uint32_t now_ms);

/* "!wifi <ssid> <password...>": the SSID is one token; the password is the REST of the
 * line after the whitespace that follows the SSID (it may contain spaces; trailing
 * spaces are kept). The password is mandatory (open networks are not supported: a
 * forgotten password must not silently store an open network). SSID 1..32 bytes,
 * password 8..64 chars. Returns 0, or -1 on a usage error (outputs zeroed). */
#define CLIP_SSID_MAX 32
#define CLIP_WIFI_PASS_MAX 64
int clip_parse_wifi(const char *line, char ssid[CLIP_SSID_MAX + 1], char pass[CLIP_WIFI_PASS_MAX + 1]);

#ifdef __cplusplus
}
#endif
