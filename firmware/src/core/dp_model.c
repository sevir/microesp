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
    {DP_PC_HOSTNAME, "pc_hostname", DPT_STR, false, 0, DPM_HOST_MAX, 0, 1000, 0},
    {DP_CMD_COUNTDOWN, "cmd_countdown", DPT_VALUE, true, 0, 60, 1, 300, 0},
    {DP_LAST_RESULT, "last_result", DPT_ENUM, false, 0, 5, 0, 300, 0},
    {DP_FAULT, "fault", DPT_BITMAP, false, 0, 0x0f, 0, 1000, 0},
    {DP_SCRIPTS, "scripts", DPT_STR, false, 0, DPM_SCRIPTS_MAX, 0, 1000, 0},
    {DP_SCRIPT_RUN, "script_run", DPT_STR, true, 0, DPM_SCRIPT_RUN_MAX, 0, 300, 0},
};

/* Offset of string DP idx in dp_model_t.str (current value; the reported copy is
 * DPM_STR_POOL further). -1 if not a string DP. */
static int str_off(int idx)
{
    if (idx < 0 || idx >= DPM_COUNT || k_dps[idx].type != DPT_STR) return -1;
    int off = 0;
    for (int i = 0; i < idx; i++)
        if (k_dps[i].type == DPT_STR) off += k_dps[i].max + 1;
    return off;
}

static char *cur_s(dp_model_t *m, int idx) { return m->str + str_off(idx); }
static char *rep_s(dp_model_t *m, int idx) { return m->str + DPM_STR_POOL + str_off(idx); }

static void copy_s(char *dst, const char *src, int max)
{
    size_t n = src ? strlen(src) : 0;
    if (n > (size_t)max) n = (size_t)max;
    if (n) memcpy(dst, src, n);
    dst[n] = 0;
}

/* Enum ranges, same order as pcs_t / wake_method_t / last_result_t (checked by the
 * host tests against pcs_name(), wake_method_name(), lr_name() and schema/dp.json). */
static const char *const k_pc_state[] = {"off", "sleep", "booting", "on_no_agent", "on", "unknown"};
static const char *const k_wake[] = {"hid", "wol", "hid_then_wol"};
static const char *const k_last[] = {"ok", "wake_sent", "wake_failed", "cmd_rejected", "agent_offline", "cancelled"};

static const char *const *enum_table(uint8_t id, int *n)
{
    switch (id) {
    case DP_PC_STATE: *n = 6; return k_pc_state;
    case DP_WAKE_METHOD: *n = 3; return k_wake;
    case DP_LAST_RESULT: *n = 6; return k_last;
    default: *n = 0; return NULL;
    }
}

const char *dpm_enum_name(uint8_t id, int32_t v)
{
    int n;
    const char *const *t = enum_table(id, &n);
    return t && v >= 0 && v < n ? t[v] : NULL;
}

int dpm_enum_parse(uint8_t id, const char *name)
{
    int n;
    const char *const *t = enum_table(id, &n);
    for (int i = 0; t && name && i < n; i++)
        if (!strcmp(name, t[i])) return i;
    return -1;
}

const dpm_desc_t *dpm_desc_by_code(const char *code)
{
    for (int i = 0; code && i < DPM_COUNT; i++)
        if (!strcmp(code, k_dps[i].code)) return &k_dps[i];
    return NULL;
}

const dpm_desc_t *dpm_desc_at(int idx) { return idx >= 0 && idx < DPM_COUNT ? &k_dps[idx] : NULL; }

int dpm_index(uint8_t id)
{
    for (int i = 0; i < DPM_COUNT; i++)
        if (k_dps[i].id == id) return i;
    return -1;
}

const dpm_desc_t *dpm_desc(uint8_t id) { return dpm_desc_at(dpm_index(id)); }

void dpm_init(dp_model_t *m)
{
    memset(m, 0, sizeof(*m));
#ifndef NDEBUG
    /* the pool size must match the string DPs of the table */
    int total = 0;
    for (int i = 0; i < DPM_COUNT; i++)
        if (k_dps[i].type == DPT_STR) total += k_dps[i].max + 1;
    if (total != DPM_STR_POOL) abort();
#endif
}

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

bool dpm_set_str(dp_model_t *m, uint8_t id, const char *s)
{
    int i = dpm_index(id);
    if (i < 0 || k_dps[i].type != DPT_STR) return false;
    if (s && strlen(s) > (size_t)k_dps[i].max) return false;
    copy_s(cur_s(m, i), s, k_dps[i].max);
    m->slot[i].valid = true;
    return true;
}

int32_t dpm_get(const dp_model_t *m, uint8_t id)
{
    int i = dpm_index(id);
    return i < 0 ? 0 : m->slot[i].v;
}

const char *dpm_get_str(const dp_model_t *m, uint8_t id)
{
    int i = dpm_index(id);
    return str_off(i) < 0 ? "" : m->str + str_off(i);
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

static bool due(dp_model_t *m, int idx, uint32_t now)
{
    const dpm_desc_t *d = &k_dps[idx];
    const dpm_slot_t *s = &m->slot[idx];
    if (!s->valid) return false;
    if (s->force) return true;
    if (!s->reported_valid) return true;
    bool changed = d->type == DPT_STR ? strcmp(cur_s(m, idx), rep_s(m, idx)) != 0 : s->v != s->reported_v;
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
        if (due(m, i, now_ms)) ids[n++] = k_dps[i].id;
    return n;
}

void dpm_mark_reported(dp_model_t *m, uint8_t id, uint32_t now_ms)
{
    int i = dpm_index(id);
    if (i < 0) return;
    m->slot[i].force = false;
    dpm_mark_reported_as(m, id, now_ms, m->slot[i].v, k_dps[i].type == DPT_STR ? cur_s(m, i) : NULL);
}

void dpm_mark_reported_as(dp_model_t *m, uint8_t id, uint32_t now_ms, int32_t v, const char *str)
{
    int i = dpm_index(id);
    if (i < 0) return;
    dpm_slot_t *s = &m->slot[i];
    s->reported_valid = true; /* force is left alone: a force set while in flight still counts */
    s->reported_v = v;
    if (k_dps[i].type == DPT_STR) {
        char *dst = rep_s(m, i);
        if (str != dst) copy_s(dst, str, k_dps[i].max);
    }
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

int dpm_decode_write_str(uint8_t id, const char *s)
{
    const dpm_desc_t *d = dpm_desc(id);
    if (!d || !d->writable || d->type != DPT_STR || !s) return -1;
    size_t n = strlen(s);
    if (n > (size_t)d->max) return -1;
    if (id == DP_SCRIPT_RUN) /* "" (no-op) or a script id: [a-z0-9_-]{1,12} */
        for (size_t k = 0; k < n; k++) {
            char c = s[k];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return -1;
        }
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
            for (const char *c = dpm_get_str(m, d->id); *c && o + 2 < n; c++) {
                if (*c == '"' || *c == '\\') buf[o++] = '\\';
                buf[o++] = ((unsigned char)*c >= 0x20) ? *c : '?';
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
