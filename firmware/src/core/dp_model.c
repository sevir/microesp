#include "dp_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ELAPSED(now, t) ((int32_t)((uint32_t)(now) - (uint32_t)(t)))

/* Telemetry policy (§5): every 30 s or on a change > 2 points (20 tenths).
 * Every min_interval is >= 300 ms, so no DP can exceed 200 reports/min. */
static const dpm_desc_t k_dps[DPM_COUNT] = {
    {DP_PC_STATE, "pc_state", DPT_ENUM, false, 0, 5, 0, 1000, 0},
    {DP_POWER_ON, "power_on", DPT_BOOL, true, 0, 1, 0, 300, 0},
    {DP_POWER_OFF, "power_off", DPT_BOOL, true, 0, 1, 0, 300, 0},
    {DP_REBOOT, "reboot", DPT_BOOL, true, 0, 1, 0, 300, 0},
    {DP_CPU, "cpu_usage", DPT_VALUE, false, 0, 1000, 20, 5000, 30000},
    {DP_MEM, "mem_usage", DPT_VALUE, false, 0, 1000, 20, 5000, 30000},
    {DP_DISK_FREE, "disk_free", DPT_VALUE, false, 0, 1000, 20, 5000, 30000},
    {DP_AGENT_ONLINE, "agent_online", DPT_BOOL, false, 0, 1, 0, 1000, 0},
    {DP_WAKE_METHOD, "wake_method", DPT_ENUM, true, 0, 2, 0, 300, 0},
    {DP_PC_UPTIME, "pc_uptime", DPT_VALUE, false, 0, 999999999, 0x7fffffff, 60000, 60000},
    {DP_PC_HOSTNAME, "pc_hostname", DPT_STR, false, 0, DPM_STR_MAX, 0, 1000, 0},
    {DP_CMD_COUNTDOWN, "cmd_countdown", DPT_VALUE, true, 0, 60, 1, 300, 0},
    {DP_LAST_RESULT, "last_result", DPT_ENUM, false, 0, 5, 0, 300, 0},
    {DP_FAULT, "fault", DPT_BITMAP, false, 0, 0x0f, 0, 1000, 0},
};

const dpm_desc_t *dpm_desc_at(int idx) { return idx >= 0 && idx < DPM_COUNT ? &k_dps[idx] : NULL; }

int dpm_index(uint8_t id)
{
    for (int i = 0; i < DPM_COUNT; i++)
        if (k_dps[i].id == id) return i;
    return -1;
}

const dpm_desc_t *dpm_desc(uint8_t id) { return dpm_desc_at(dpm_index(id)); }

void dpm_init(dp_model_t *m) { memset(m, 0, sizeof(*m)); }

void dpm_set(dp_model_t *m, uint8_t id, int32_t v)
{
    int i = dpm_index(id);
    if (i < 0 || k_dps[i].type == DPT_STR) return;
    const dpm_desc_t *d = &k_dps[i];
    if (d->type == DPT_BITMAP) v &= d->max;
    else if (v < d->min) v = d->min;
    else if (v > d->max) v = d->max;
    m->slot[i].v = v;
    m->slot[i].valid = true;
}

void dpm_set_str(dp_model_t *m, uint8_t id, const char *s)
{
    int i = dpm_index(id);
    if (i < 0 || k_dps[i].type != DPT_STR) return;
    snprintf(m->slot[i].s, sizeof(m->slot[i].s), "%s", s ? s : "");
    m->slot[i].valid = true;
}

int32_t dpm_get(const dp_model_t *m, uint8_t id)
{
    int i = dpm_index(id);
    return i < 0 ? 0 : m->slot[i].v;
}

const char *dpm_get_str(const dp_model_t *m, uint8_t id)
{
    int i = dpm_index(id);
    return i < 0 ? "" : m->slot[i].s;
}

void dpm_force_all(dp_model_t *m)
{
    for (int i = 0; i < DPM_COUNT; i++)
        if (m->slot[i].valid) m->slot[i].force = true;
}

void dpm_force(dp_model_t *m, uint8_t id)
{
    int i = dpm_index(id);
    if (i >= 0 && m->slot[i].valid) m->slot[i].force = true;
}

static bool due(const dpm_desc_t *d, const dpm_slot_t *s, uint32_t now)
{
    if (!s->valid) return false;
    if (s->force) return true;
    if (!s->reported_valid) return true;
    bool changed = d->type == DPT_STR ? strcmp(s->s, s->reported_s) != 0 : s->v != s->reported_v;
    if (!changed) return false;
    int32_t since = ELAPSED(now, s->last_ms);
    if (since < (int32_t)d->min_interval_ms) return false;
    if (d->type != DPT_VALUE || d->periodic_ms == 0) return true;
    if (labs((long)s->v - (long)s->reported_v) >= d->threshold) return true;
    return since >= (int32_t)d->periodic_ms;
}

int dpm_collect(dp_model_t *m, uint32_t now_ms, uint8_t *ids, int max)
{
    int n = 0;
    for (int i = 0; i < DPM_COUNT && n < max; i++)
        if (due(&k_dps[i], &m->slot[i], now_ms)) ids[n++] = k_dps[i].id;
    return n;
}

void dpm_mark_reported(dp_model_t *m, uint8_t id, uint32_t now_ms)
{
    int i = dpm_index(id);
    if (i < 0) return;
    m->slot[i].force = false;
    dpm_mark_reported_as(m, id, now_ms, m->slot[i].v, m->slot[i].s);
}

void dpm_mark_reported_as(dp_model_t *m, uint8_t id, uint32_t now_ms, int32_t v, const char *str)
{
    int i = dpm_index(id);
    if (i < 0) return;
    dpm_slot_t *s = &m->slot[i];
    s->reported_valid = true; /* force is left alone: a force set while in flight still counts */
    s->reported_v = v;
    snprintf(s->reported_s, sizeof(s->reported_s), "%s", str ? str : "");
    s->last_ms = now_ms;
    s->reports++;
    m->total_reports++;
}

int dpm_decode_write(uint8_t id, dpt_t type, int32_t raw, int32_t *out)
{
    const dpm_desc_t *d = dpm_desc(id);
    if (!d || !d->writable || d->type != type) return -1;
    if (type == DPT_BOOL) raw = raw ? 1 : 0;
    if (raw < d->min || raw > d->max) return -1;
    *out = raw;
    return 0;
}

int dpm_to_json(const dp_model_t *m, char *buf, size_t n)
{
    size_t o = 0;
    int w = snprintf(buf, n, "{");
    if (w < 0) return 0;
    o = (size_t)w;
    bool first = true;
    for (int i = 0; i < DPM_COUNT && o < n; i++) {
        const dpm_slot_t *s = &m->slot[i];
        if (!s->valid) continue;
        const dpm_desc_t *d = &k_dps[i];
        if (d->type == DPT_STR) {
            /* hostnames are validated upstream; escape quotes/backslashes anyway */
            w = snprintf(buf + o, n - o, "%s\"%u\":\"", first ? "" : ",", d->id);
            if (w < 0) break;
            o += (size_t)w;
            for (const char *c = s->s; *c && o + 2 < n; c++) {
                if (*c == '"' || *c == '\\') buf[o++] = '\\';
                buf[o++] = (*c >= 0x20) ? *c : '?';
            }
            if (o + 1 < n) buf[o++] = '"';
            buf[o < n ? o : n - 1] = 0;
        } else if (d->type == DPT_BOOL) {
            w = snprintf(buf + o, n - o, "%s\"%u\":%s", first ? "" : ",", d->id, s->v ? "true" : "false");
            if (w < 0) break;
            o += (size_t)w;
        } else {
            w = snprintf(buf + o, n - o, "%s\"%u\":%ld", first ? "" : ",", d->id, (long)s->v);
            if (w < 0) break;
            o += (size_t)w;
        }
        first = false;
    }
    if (o + 1 < n) {
        buf[o++] = '}';
        buf[o] = 0;
    } else if (n) {
        buf[n - 1] = 0;
        o = n - 1;
    }
    return (int)o;
}
