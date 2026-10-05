/*
 * MicroESP — user script run flow (cdc-v1 §3.1, DP 116 script_run), pure C and
 * host-tested.
 *
 *   request(id) --no authenticated agent session--> AGENT_OFFLINE
 *        | --unknown id / a script cmd already awaiting its ack / send failed--> CMD_REJECTED
 *        v
 *   WAIT_ACK (cmd script:<id> sent) --ack ok--> OK | --ack !ok / 10 s timeout /
 *                                                     session dropped--> CMD_REJECTED
 *
 * Independent of the shutdown/reboot flow (core/powercmd.c): no countdown, and the
 * link keeps one pending cmd per class, so a script run never delays or cancels a
 * running shutdown/reboot countdown (nor the other way round). The result goes to DP
 * 113 last_result; DP 116 is reported back to "" by the caller after every request.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "link_proto.h"
#include "powercmd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* "script:<id>" -> cmd id, 0 on failure */
    uint32_t (*send_cmd)(void *ctx, const char *action);
    void (*result)(void *ctx, const char *script_id, last_result_t r);
    void *ctx;
} scr_cbs_t;

typedef struct {
    scr_cbs_t cb;
    bool waiting; /* cmd sent, awaiting its ack */
    uint32_t cmd_id;
    char id[LINK_SCRIPT_ID_MAX + 1];
    uint32_t runs, ok, rejected, offline; /* stats */
} scr_t;

void scr_init(scr_t *s, const scr_cbs_t *cb);
/* Run script `id`. agent_ready: authenticated session with the agent online; known:
 * id is in the agent's current list. Every request ends in exactly one result()
 * call, now (offline / rejected) or later (scr_on_ack / scr_on_ack_timeout). Returns
 * true if the cmd was sent and the flow now waits for the ack. */
bool scr_request(scr_t *s, const char *id, bool agent_ready, bool known);
void scr_on_ack(scr_t *s, uint32_t cmd_id, bool ok);
void scr_on_ack_timeout(scr_t *s, uint32_t cmd_id);

#ifdef __cplusplus
}
#endif
