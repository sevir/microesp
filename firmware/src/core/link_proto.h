/*
 * MicroESP — dongle side of the cdc-v1 protocol (docs/protocol/cdc-v1.md, NORMATIVE).
 *
 * Pure C (cJSON + mesp_crypto), no RTOS/IDF dependency: the firmware glue
 * (src/agent_link.c) feeds received lines and a millisecond clock; everything is
 * host-tested in test/host.
 *
 * Not thread-safe: call every function from the same task.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesp_crypto.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LINK_MAX_LINE        512 /* bytes per line incl. '\n' */
#define LINK_MAX_HOST        64
#define LINK_MAX_MACS        4
#define LINK_MAX_DEPTH       4     /* JSON nesting limit (deepest valid message: hello.macs = 2) */
#define LINK_ONLINE_TIMEOUT  15000 /* ms without agent messages -> agent_online=false */
#define LINK_ACK_TIMEOUT     10000 /* ms without ack -> cmd_rejected */
#define LINK_PAIR_WINDOW     120000
#define LINK_PAIR_MAX_FAILS  3
/* User scripts (cdc-v1 §3.1) */
#define LINK_MAX_SCRIPTS       5
#define LINK_SCRIPT_ID_MAX     12 /* ^[a-z0-9_-]{1,12}$ */
#define LINK_SCRIPT_LABEL_MAX  24 /* bytes of UTF-8 */
#define LINK_SCRIPT_PREFIX     "script:"
#define LINK_ACTION_MAX        (7 + LINK_SCRIPT_ID_MAX) /* "script:<id>" */
/* [["<id>","<label>"],...] for 5 items at their maximum lengths */
#define LINK_SCRIPTS_JSON_MAX  (2 + LINK_MAX_SCRIPTS * (7 + LINK_SCRIPT_ID_MAX + LINK_SCRIPT_LABEL_MAX) + LINK_MAX_SCRIPTS - 1)

typedef enum {
    LINK_EV_HELLO,       /* authenticated hello data (emitted on ready): host, macs */
    LINK_EV_READY,       /* session authenticated */
    LINK_EV_ONLINE,      /* agent_online changed: .online */
    LINK_EV_TELE,        /* telemetry */
    LINK_EV_ACK,         /* ack for the pending cmd: .ack */
    LINK_EV_ACK_TIMEOUT, /* no ack within LINK_ACK_TIMEOUT, or the session was dropped
                            with a cmd pending: .ack.id, .ack.err */
    LINK_EV_PAIRED,      /* new key stored (must be persisted by save_key) */
    LINK_EV_PAIR_FAIL,   /* wrong code: .pair_fails */
    LINK_EV_PAIR_END,    /* pairing mode left (timeout / too many failures / paired) */
    LINK_EV_PROTO_ERR,   /* an err{code} was sent to the agent: .err_code */
    LINK_EV_SCRIPTS,     /* a valid scripts{list} replaced link_t.scripts (see link_scripts_json) */
} link_ev_type_t;

typedef struct {
    link_ev_type_t type;
    union {
        bool online;
        struct {
            const char *host;
            uint8_t macs[LINK_MAX_MACS][6];
            int nmacs;
        } hello;
        struct {
            uint32_t seq, uptime;
            int cpu, mem, disk_free;
        } tele;
        struct {
            uint32_t id;
            bool ok;
            const char *err; /* "" if absent */
        } ack;
        int pair_fails;
        const char *err_code;
    };
} link_ev_t;

typedef struct {
    void (*send)(void *ctx, const char *line);                /* one JSON object, WITHOUT '\n' */
    void (*random)(void *ctx, uint8_t *buf, size_t n);         /* CSPRNG */
    bool (*save_key)(void *ctx, const uint8_t key[MC_KEY_LEN]); /* persist new pairing key */
    void (*event)(void *ctx, const link_ev_t *ev);
    void *ctx;
} link_cbs_t;

typedef enum { LINK_IDLE, LINK_WAIT_AUTH, LINK_READY } link_state_t;

/* Commands awaiting an ack: at most one per class, so a script run never blocks (nor is
 * blocked by) a shutdown/reboot. Ids are shared (one increasing counter per session). */
typedef enum { LINK_CMD_POWER = 0, LINK_CMD_SCRIPT, LINK_CMD__CLASSES } link_cmd_class_t;

typedef struct {
    bool pending;
    uint32_t id;
    uint32_t sent_ms;
} link_pend_t;

typedef struct {
    char id[LINK_SCRIPT_ID_MAX + 1];
    char label[LINK_SCRIPT_LABEL_MAX + 1];
} link_script_t;

typedef struct {
    link_cbs_t cb;
    char fw[24];
    char dev[13];
    /* key */
    bool has_key;
    uint8_t key[MC_KEY_LEN];
    /* session */
    link_state_t st;
    char na[MC_NONCE_HEX + 1], nd[MC_NONCE_HEX + 1];
    char pend_host[LINK_MAX_HOST + 1];
    uint8_t pend_macs[LINK_MAX_MACS][6];
    int pend_nmacs;
    uint32_t last_rx_ms;
    bool online;
    uint32_t cmd_id;    /* last id sent in this session */
    link_pend_t pend[LINK_CMD__CLASSES];
    /* user scripts of the agent (RAM only; kept when the session drops) */
    bool scripts_known; /* a valid list was received at least once */
    int nscripts;
    link_script_t scripts[LINK_MAX_SCRIPTS];
    /* pairing */
    bool pair_mode;
    char pair_code[7];
    uint32_t pair_until_ms;
    int pair_fails;
    bool pair_chal;
    char pna[MC_NONCE_HEX + 1], pnd[MC_NONCE_HEX + 1];
    /* stats */
    uint32_t rx_lines, rx_bad, sessions;
} link_t;

void link_init(link_t *l, const link_cbs_t *cb, const char *fw, const char *dev12hex, const uint8_t *key /*NULL=none*/);

/* Feed one received line (without terminator, may contain '\r'). */
void link_rx_line(link_t *l, const char *line, size_t len, uint32_t now_ms);
/* The transport discarded a line longer than LINK_MAX_LINE. */
void link_rx_too_long(link_t *l, uint32_t now_ms);
/* Periodic timeouts (call >= every 100 ms). */
void link_tick(link_t *l, uint32_t now_ms);

/* The transport closed (host dropped DTR / port closed): drop the session so a later
 * opener must authenticate again. */
void link_close_session(link_t *l);

/* Forget the key (unpair); the session is dropped. */
void link_forget_key(link_t *l);

bool link_ready(const link_t *l);
bool link_online(const link_t *l);

/* Send a countdown notice (action: shutdown/reboot/cancel). Returns false if no session. */
bool link_send_notice(link_t *l, const char *action, int in_s);
/* Send a signed cmd: "shutdown", "reboot" or "script:<id>" (id of the current list).
 * Returns the id (>0) or 0 if no ready session, unknown action, or a cmd of the same
 * class (power / script) is still awaiting its ack. */
uint32_t link_send_cmd(link_t *l, const char *action, uint32_t now_ms);
/* A cmd of that class is awaiting its ack. */
bool link_cmd_pending(const link_t *l, link_cmd_class_t c);

/* ^[a-z0-9_-]{1,12}$ */
bool link_valid_script_id(const char *id);
/* cdc-v1 §3.1 label: 1..24 bytes of valid UTF-8, no control characters (C0, DEL, C1),
 * no '"' and no '\'. */
bool link_valid_script_label(const char *label);
/* Script of the current list, NULL if unknown. */
const link_script_t *link_script_find(const link_t *l, const char *id);
/* Current list as compact JSON [["<id>","<label>"],...] (DP 115). Ids and labels are
 * validated on receipt and need no escaping. Returns the length, -1 if it does not fit
 * (cap >= LINK_SCRIPTS_JSON_MAX + 1 always fits). */
int link_scripts_json(const link_t *l, char *out, size_t cap);

/* Pairing mode with the given 6-digit code for LINK_PAIR_WINDOW ms. */
void link_pair_start(link_t *l, const char *code, uint32_t now_ms);
void link_pair_stop(link_t *l);
bool link_pairing(const link_t *l);
uint32_t link_pair_remaining_ms(const link_t *l, uint32_t now_ms);

/* MAC "aa:bb:cc:dd:ee:ff" (lowercase) -> 6 bytes. 0 on success. */
int link_parse_mac(const char *s, uint8_t mac[6]);

#ifdef __cplusplus
}
#endif
