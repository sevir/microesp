/*
 * MicroESP — PC state machine (DP 101 pc_state), pure C and host-tested.
 *
 * Inputs (sampled every tick): USB mounted/suspended (TinyUSB callbacks), agent
 * session online (heartbeat, 15 s timeout) and "wake in progress".
 *
 * Raw classification (first match wins):
 *   agent online                                   -> ON
 *   wake in progress and not (mounted & !suspended) -> BOOTING
 *   shutdown expected                              -> BOOTING if a wake is in progress, else OFF
 *                                                     (a shutdown cmd was acked: with a powered
 *                                                     port S5 shows a suspend or a re-enumerated,
 *                                                     active bus)
 *   mounted & suspended                            -> SLEEP
 *   !mounted                                       -> OFF
 *   mounted & !suspended & agent seen since the last up-edge  -> ON_NO_AGENT (agent lost)
 *   mounted & !suspended & < BOOT_GRACE since up-edge          -> BOOTING
 *   mounted & !suspended                           -> ON_NO_AGENT
 * (up-edge = mount, resume or wake start)
 *
 * Hysteresis: a raw state must be stable for hold(raw) before it becomes the
 * reported state: ON/BOOTING 0 s, ON_NO_AGENT 2 s, SLEEP/OFF 3 s. UNKNOWN is only
 * the initial state; it is left after STARTUP_SETTLE.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Values match the DP 101 enum order. */
typedef enum { PCS_OFF = 0, PCS_SLEEP, PCS_BOOTING, PCS_ON_NO_AGENT, PCS_ON, PCS_UNKNOWN, PCS__COUNT } pcs_t;

#define PCS_BOOT_GRACE_MS     90000
#define PCS_HOLD_OFF_MS       3000
#define PCS_HOLD_SLEEP_MS     3000
#define PCS_HOLD_NOAGENT_MS   2000
#define PCS_STARTUP_SETTLE_MS 3000

typedef struct {
    bool mounted, suspended, agent_online, wake_in_progress;
    bool shutdown_expected; /* set after an acked shutdown cmd, cleared when an OS is back
                             * (agent session ready or CDC port opened by the host) */
} pcs_inputs_t;

typedef struct {
    pcs_t state;
    pcs_t cand;
    uint32_t cand_since;
    uint32_t boot_ms;
    uint32_t up_edge_ms;      /* last mount / resume / wake start */
    bool agent_since_up;      /* agent was online since the last up-edge */
    bool agent_lost;          /* fault: agent lost while the PC stays on */
    pcs_inputs_t prev;
    bool have_prev;
    uint32_t changes;
} pcs_sm_t;

void pcs_init(pcs_sm_t *sm, uint32_t now_ms);
/* Returns true if the reported state changed. */
bool pcs_update(pcs_sm_t *sm, const pcs_inputs_t *in, uint32_t now_ms);
pcs_t pcs_raw(const pcs_sm_t *sm, const pcs_inputs_t *in, uint32_t now_ms);
const char *pcs_name(pcs_t s);

#ifdef __cplusplus
}
#endif
