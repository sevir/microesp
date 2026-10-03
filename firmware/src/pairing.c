/*
 * MicroESP — agent pairing mode (US-0023, cdc-v1 §4).
 *
 * Entered at boot when no key is stored, or by a 3..5 s button hold (physical
 * presence). "!pair" / "!unpair" exist only in development builds (MESP_DEV_CLI).
 * A fresh uniformly random 6-digit code is shown ONLY on the display for 120 s: it is
 * never logged nor sent over the CDC (it is also registered with the log redactor as
 * defence in depth). 3 wrong codes end the mode (no automatic re-entry).
 */
#include <stdio.h>
#include <string.h>

#include "log_redact.h"
#include "mesp_hal.h"
#include "modules.h"
#include "tal_api.h"

static char s_code[7];

static void new_code(void)
{
    uint32_t r;
    do {
        mhal_random(&r, sizeof(r));
    } while (r >= 4294000000u); /* 4294000000 = 4294 * 10^6: unbiased modulo */
    snprintf(s_code, sizeof(s_code), "%06lu", (unsigned long)(r % 1000000u));
}

void pairing_start(const char *why)
{
    new_code();
    lr_set_secret(LR_SLOT_PAIR_CODE, s_code);
    link_pair_start(&g_app.link, s_code, app_now_ms());
    PR_NOTICE("agent pairing mode ON (%s) for %d s, code on the display", why, LINK_PAIR_WINDOW / 1000);
}

void pairing_stop(void) { link_pair_stop(&g_app.link); }

void pairing_on_end(void)
{
    PR_NOTICE("agent pairing mode OFF");
    memset(s_code, 0, sizeof(s_code));
    lr_set_secret(LR_SLOT_PAIR_CODE, NULL);
}

void pairing_forget(void)
{
    link_forget_key(&g_app.link);
    if (mhal_nvs_erase("agent_key") != 0) PR_ERR("agent key: NVS erase failed (the key returns after a reboot)");
    else PR_NOTICE("agent key forgotten");
}

bool pairing_active(void) { return link_pairing(&g_app.link); }
const char *pairing_code(void) { return s_code; }
int pairing_remaining_s(void) { return (int)((link_pair_remaining_ms(&g_app.link, app_now_ms()) + 999) / 1000); }

void pairing_init(void)
{
    if (!g_app.link.has_key) pairing_start("no key stored");
}
