#include "cli_policy.h"

#include <string.h>

#define ELAPSED(now, t) ((int32_t)((uint32_t)(now) - (uint32_t)(t)))

static const char *const k_release[] = {"!help", "!status", "!version", "!dp", "!log",
                                        "!dfu",  "!usj",    "!reboot",  "!cancel"};
static const char *const k_dev[] = {"!pair", "!unpair", "!wake", "!method", "!countdown", "!cmd", "!key"};
static const char *const k_obsolete[] = {"!auth", "!pid", "!reset-tuya"};

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
    if (!strcmp(cmd, "!tylink"))
        return c->dev || !c->tylink_provisioned || c->prov_window ? CLIP_ALLOW : CLIP_LOCKED;
    if (!strcmp(cmd, "!wifi")) return c->dev || !c->wifi_provisioned || c->prov_window ? CLIP_ALLOW : CLIP_LOCKED;
    if (in(cmd, k_dev, sizeof(k_dev) / sizeof(k_dev[0]))) return c->dev ? CLIP_ALLOW : CLIP_DEV_ONLY;
    if (in(cmd, k_obsolete, sizeof(k_obsolete) / sizeof(k_obsolete[0]))) return CLIP_OBSOLETE;
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

static bool ws(char ch) { return ch == ' ' || ch == '\t'; }

int clip_parse_wifi(const char *line, char ssid[CLIP_SSID_MAX + 1], char pass[CLIP_WIFI_PASS_MAX + 1])
{
    ssid[0] = 0;
    pass[0] = 0;
    if (!line) return -1;
    const char *p = line;
    while (ws(*p)) p++;
    if (strncmp(p, "!wifi", 5) || (p[5] && !ws(p[5]))) return -1;
    p += 5;
    while (ws(*p)) p++;
    const char *s = p;
    while (*p && !ws(*p)) p++;
    size_t sl = (size_t)(p - s);
    while (ws(*p)) p++;
    size_t pl = strlen(p);
    for (size_t i = 0; i < pl; i++)
        if ((unsigned char)p[i] < 0x20 || p[i] == 0x7f) return -1;
    if (sl == 0 || sl > CLIP_SSID_MAX || pl < 8 || pl > CLIP_WIFI_PASS_MAX) return -1;
    memcpy(ssid, s, sl);
    ssid[sl] = 0;
    memcpy(pass, p, pl);
    pass[pl] = 0;
    return 0;
}
