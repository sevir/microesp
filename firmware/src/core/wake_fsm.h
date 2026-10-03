/*
 * MicroESP — power-on ("wake") sequencing (US-0014), pure C and host-tested.
 *
 * Methods (DP 109): hid, wol, hid_then_wol.
 *  - PC already up (mounted & !suspended)  -> ignored.
 *  - HID: bus suspended with remote wakeup armed -> tud_remote_wakeup();
 *         otherwise (not mounted / not armed) -> forced resume signalling
 *         (K-state, best effort for S4/S5 with "USB wake"/"Always-on USB" BIOS),
 *         retried at +2 s and +4 s.
 *  - WOL: magic packet to every MAC learnt from hello (UDP broadcast 9 and 7).
 *  - hid_then_wol: HID, then WOL if the bus is not up after 20 s.
 *  - Success = bus up (mounted & !suspended). No success in 120 s ->
 *    last_result=wake_failed + fault bit wake_failed (cleared on the next success).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { WM_HID = 0, WM_WOL, WM_HID_THEN_WOL, WM__COUNT } wake_method_t;

#define WAKE_WOL_AFTER_MS   20000
#define WAKE_TIMEOUT_MS     120000
#define WAKE_HID_RETRY_MS   2000
#define WAKE_HID_RETRIES    2

typedef enum { WAKE_STARTED = 0, WAKE_IGNORED_ON, WAKE_BUSY, WAKE_NO_TARGET } wake_rc_t;

typedef struct {
    bool mounted, suspended, rwu_armed;
} wake_usb_t;

typedef struct {
    int (*hid_remote_wakeup)(void *ctx); /* 0 = signalled */
    int (*hid_force_resume)(void *ctx);  /* 0 = signalled */
    int (*wol_send)(void *ctx);          /* number of MACs targeted (0 = none known) */
    void (*sent)(void *ctx);             /* last_result=wake_sent */
    void (*done)(void *ctx, bool ok);    /* success, or wake_failed after the timeout */
    void *ctx;
} wake_cbs_t;

typedef struct {
    wake_cbs_t cb;
    bool active;
    wake_method_t method;
    uint32_t start_ms;
    bool wol_sent;
    bool force_path;
    int hid_retries;
    uint32_t last_hid_ms;
    bool fault_failed;
    uint32_t attempts, successes, failures;
} wake_t;

const char *wake_method_name(wake_method_t m);
int wake_method_parse(const char *s); /* -1 if unknown */

void wake_init(wake_t *w, const wake_cbs_t *cb);
wake_rc_t wake_request(wake_t *w, wake_method_t m, const wake_usb_t *u, uint32_t now_ms);
void wake_tick(wake_t *w, const wake_usb_t *u, uint32_t now_ms);
/* Human readable plan for a dry run ("!wake" without "force"). */
const char *wake_plan(wake_method_t m, const wake_usb_t *u);

/* Wake-on-LAN magic packet: 6 x 0xFF + 16 x MAC (102 bytes). */
#define WOL_PKT_LEN 102
void wol_build(const uint8_t mac[6], uint8_t out[WOL_PKT_LEN]);

#ifdef __cplusplus
}
#endif
