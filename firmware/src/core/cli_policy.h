/*
 * MicroESP — CDC CLI access policy (security hardening), pure C and host-tested.
 *
 * Anyone who can open the CDC port (dialout/root on the PC) can type CLI lines, so a
 * RELEASE build (MESP_DEV_CLI=n) only accepts read-only/recovery commands:
 *     !help !status !version !dp !log !dfu !usj !reboot !cancel
 * Tuya provisioning (!auth, !pid) is accepted in release only while that item is not
 * provisioned yet, or inside the 120 s provisioning window opened by a physical
 * gesture (button held 5..10 s). Everything else (!pair !unpair !wake !method
 * !countdown !cmd !key !reset-tuya) is development-only (MESP_DEV_CLI=y,
 * "./build.sh dev").
 */
#pragma once
#include <stdbool.h>
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
} clip_rc_t;

typedef struct {
    bool dev;               /* MESP_DEV_CLI build */
    bool creds_provisioned; /* Tuya UUID/AuthKey already stored (NVS, license, secrets.h) */
    bool pid_provisioned;   /* Tuya PID already stored */
    bool prov_window;       /* physical provisioning window open */
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

#ifdef __cplusplus
}
#endif
