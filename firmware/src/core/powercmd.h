/*
 * MicroESP — shutdown/reboot command flow (US-0016), pure C and host-tested.
 *
 *   request(action) --agent offline--> result AGENT_OFFLINE
 *        | agent online
 *        v
 *   COUNTDOWN (DP 113 s, notice{action,in} sent) --cancel--> notice{cancel}, CANCELLED
 *        | expires (agent still online, else AGENT_OFFLINE)
 *        v
 *   WAIT_ACK (cmd sent) --ack ok--> OK | --ack !ok / 10 s timeout--> CMD_REJECTED
 *
 * Every terminal result resets DP 103/104 to false (reset_dps output).
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* DP 114 last_result enum order */
typedef enum {
    LR_OK = 0,
    LR_WAKE_SENT,
    LR_WAKE_FAILED,
    LR_CMD_REJECTED,
    LR_AGENT_OFFLINE,
    LR_CANCELLED,
    LR__COUNT
} last_result_t;

typedef enum { PWR_NONE = 0, PWR_SHUTDOWN, PWR_REBOOT } pwr_action_t;
typedef enum { PWR_IDLE = 0, PWR_COUNTDOWN, PWR_WAIT_ACK } pwr_state_t;

typedef struct {
    bool (*send_notice)(void *ctx, const char *action, int in_s);
    uint32_t (*send_cmd)(void *ctx, const char *action); /* id or 0 on failure */
    void (*result)(void *ctx, pwr_action_t a, last_result_t r);
    void (*reset_dps)(void *ctx); /* DP 103/104 back to false */
    void *ctx;
} pwr_cbs_t;

typedef struct {
    pwr_cbs_t cb;
    pwr_state_t st;
    pwr_action_t action;
    uint32_t deadline_ms;
    uint32_t cmd_id;
} pwr_t;

const char *pwr_action_name(pwr_action_t a);
const char *lr_name(last_result_t r);

void pwr_init(pwr_t *p, const pwr_cbs_t *cb);
/* Returns false if ignored (busy). */
bool pwr_request(pwr_t *p, pwr_action_t a, int countdown_s, bool agent_online, uint32_t now_ms);
/* Cancel a running countdown. Returns true if something was cancelled. */
bool pwr_cancel(pwr_t *p);
void pwr_tick(pwr_t *p, bool agent_online, uint32_t now_ms);
void pwr_on_ack(pwr_t *p, uint32_t id, bool ok);
void pwr_on_ack_timeout(pwr_t *p, uint32_t id);
/* Seconds left in the countdown (rounded up), 0 if not counting. */
int pwr_remaining_s(const pwr_t *p, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
