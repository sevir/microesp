#include "cli_policy.h"

#include <string.h>

#define ELAPSED(now, t) ((int32_t)((uint32_t)(now) - (uint32_t)(t)))

static const char *const k_release[] = {"!help", "!status", "!version", "!dp", "!log",
                                        "!dfu",  "!usj",    "!reboot",  "!cancel"};
static const char *const k_dev[] = {"!pair", "!unpair", "!wake", "!method", "!countdown",
                                    "!cmd",  "!key",    "!reset-tuya"};

static bool in(const char *cmd, const char *const *list, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (!strcmp(cmd, list[i])) return true;
    return false;
}

clip_rc_t clip_check(const char *cmd, const clip_ctx_t *c)
{
    if (!cmd || !c) return CLIP_UNKNOWN;
    if (in(cmd, k_release, sizeof(k_release) / sizeof(k_release[0]))) return CLIP_ALLOW;
    if (!strcmp(cmd, "!auth"))
        return c->dev || !c->creds_provisioned || c->prov_window ? CLIP_ALLOW : CLIP_LOCKED;
    if (!strcmp(cmd, "!pid")) return c->dev || !c->pid_provisioned || c->prov_window ? CLIP_ALLOW : CLIP_LOCKED;
    if (in(cmd, k_dev, sizeof(k_dev) / sizeof(k_dev[0]))) return c->dev ? CLIP_ALLOW : CLIP_DEV_ONLY;
    return CLIP_UNKNOWN;
}

void clip_window_open(clip_window_t *w, uint32_t now_ms)
{
    w->open = true;
    w->until_ms = now_ms + CLIP_PROV_WINDOW_MS;
}

void clip_window_close(clip_window_t *w) { w->open = false; }

bool clip_window_active(clip_window_t *w, uint32_t now_ms)
{
    if (w->open && ELAPSED(now_ms, w->until_ms) >= 0) w->open = false;
    return w->open;
}

int clip_window_remaining_s(const clip_window_t *w, uint32_t now_ms)
{
    if (!w->open) return 0;
    int32_t ms = ELAPSED(w->until_ms, now_ms);
    return ms <= 0 ? 0 : (int)((ms + 999) / 1000);
}
