/*
 * MicroESP — typed Tuya DP table (docs/analysis/00-architecture-analysis.md §5) with
 * change thresholds and throttling, plus validation of DPs received from the cloud.
 * Pure C, host-tested; core/tylink.c maps it onto TuyaLink property codes/JSON (the
 * numeric ids are the Tuya abilityIds, kept as the internal key and in the docs).
 * Machine-readable description: firmware/schema/dp.json (keep in sync).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DP_PC_STATE = 101,
    DP_POWER_ON = 102,
    DP_POWER_OFF = 103,
    DP_REBOOT = 104,
    DP_CPU = 105,
    DP_MEM = 106,
    DP_DISK_FREE = 107,
    DP_AGENT_ONLINE = 108,
    DP_WAKE_METHOD = 109,
    DP_PC_UPTIME = 110,
    DP_PC_HOSTNAME = 111,
    DP_CMD_COUNTDOWN = 112,
    DP_LAST_RESULT = 113,
    DP_FAULT = 114,
    DP_SCRIPTS = 115,
    DP_SCRIPT_RUN = 116,
};

/* DP 114 fault bits */
#define FAULT_AGENT_LOST    (1u << 0)
#define FAULT_WAKE_FAILED   (1u << 1)
#define FAULT_HID_NOT_ARMED (1u << 2)
#define FAULT_CLOUD_LOST    (1u << 3)

typedef enum { DPT_BOOL = 0, DPT_VALUE, DPT_ENUM, DPT_STR, DPT_BITMAP } dpt_t;

/* String DPs: maximum length (bytes, without NUL) of each one; dpm_desc_t.max holds
 * the same value. Only string DPs own string storage (dp_model_t.str), so the 255-byte
 * scripts DP does not grow every slot. */
#define DPM_HOST_MAX       64  /* 111 pc_hostname */
#define DPM_SCRIPTS_MAX    255 /* 115 scripts (compact JSON, <= 221 bytes in practice) */
#define DPM_SCRIPT_RUN_MAX 12  /* 116 script_run (a script id) */
#define DPM_STR_MAX        DPM_SCRIPTS_MAX    /* longest string DP (buffer sizing) */
#define DPM_WSTR_MAX       DPM_SCRIPT_RUN_MAX /* longest writable string DP */
/* Storage for one copy of every string DP (value + NUL each). */
#define DPM_STR_POOL ((DPM_HOST_MAX + 1) + (DPM_SCRIPTS_MAX + 1) + (DPM_SCRIPT_RUN_MAX + 1))

typedef struct {
    uint8_t id;
    const char *code;
    dpt_t type;
    bool writable;
    int32_t min, max;          /* value range; enum: 0..count-1; bitmap: 0..mask; string: max length */
    int32_t threshold;         /* value: minimum |delta| for an early report */
    uint32_t min_interval_ms;  /* throttle between two reports of this DP */
    uint32_t periodic_ms;      /* value: report any change at most this often (0 = on change) */
} dpm_desc_t;

#define DPM_COUNT 16

typedef struct {
    bool valid, reported_valid, force;
    int32_t v, reported_v;
    uint32_t last_ms;
    uint32_t reports;
} dpm_slot_t;

typedef struct {
    dpm_slot_t slot[DPM_COUNT];
    uint32_t total_reports;
    /* string DPs: current values in the first half, last reported values in the second
     * (each DP at a fixed offset, see dp_model.c) */
    char str[2 * DPM_STR_POOL];
} dp_model_t;

const dpm_desc_t *dpm_desc(uint8_t id);
const dpm_desc_t *dpm_desc_at(int idx);
int dpm_index(uint8_t id);

void dpm_init(dp_model_t *m);
/* Set a bool/value/enum/bitmap DP (clamped to its range). */
void dpm_set(dp_model_t *m, uint8_t id, int32_t v);
/* Set a string DP. Returns false (DP unchanged) if s is longer than the DP maximum. */
bool dpm_set_str(dp_model_t *m, uint8_t id, const char *s);
int32_t dpm_get(const dp_model_t *m, uint8_t id);
const char *dpm_get_str(const dp_model_t *m, uint8_t id);
/* Report every valid DP at the next collect (cloud (re)connected). */
void dpm_force_all(dp_model_t *m);
/* Force one DP (e.g. echo a write even if unchanged). */
void dpm_force(dp_model_t *m, uint8_t id);
/* DP ids that must be reported now (<= max). The caller reports them and then calls
 * dpm_mark_reported() for each one that was accepted. */
int dpm_collect(dp_model_t *m, uint32_t now_ms, uint8_t *ids, int max);
void dpm_mark_reported(dp_model_t *m, uint8_t id, uint32_t now_ms);
/* Same, for a report sent asynchronously: v/s are the values that were actually sent
 * (the DP may have changed meanwhile and must then stay due). s is ignored for
 * non-string DPs. Unlike dpm_mark_reported() it does not clear the force flag (the
 * caller clears it when it takes the snapshot). */
void dpm_mark_reported_as(dp_model_t *m, uint8_t id, uint32_t now_ms, int32_t v, const char *s);

/* Enum DPs: value <-> name (TuyaLink sends/receives enums as strings). NULL / -1 if
 * the DP is not an enum or the value/name is unknown. */
const char *dpm_enum_name(uint8_t id, int32_t v);
int dpm_enum_parse(uint8_t id, const char *name);
/* DP by TuyaLink property code, NULL if unknown. */
const dpm_desc_t *dpm_desc_by_code(const char *code);

/* Validate a DP written from the cloud. Returns 0 and the normalised value, or -1. */
int dpm_decode_write(uint8_t id, dpt_t type, int32_t raw, int32_t *out);
/* Same for a writable string DP: 0 if s is acceptable (length within the DP maximum,
 * DP-specific charset: script_run is "" or [a-z0-9_-]{1,12}), -1 otherwise. */
int dpm_decode_write_str(uint8_t id, const char *s);

/* {"101":4,"112":"host",...} of the current values (debug / CLI). Returns length. */
int dpm_to_json(const dp_model_t *m, char *buf, size_t n);

#ifdef __cplusplus
}
#endif
