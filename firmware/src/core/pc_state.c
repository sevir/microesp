#include "pc_state.h"

#include <string.h>

#define ELAPSED(now, t) ((int32_t)((uint32_t)(now) - (uint32_t)(t)))

static const char *const k_names[PCS__COUNT] = {"off", "sleep", "booting", "on_no_agent", "on", "unknown"};

const char *pcs_name(pcs_t s) { return (unsigned)s < PCS__COUNT ? k_names[s] : "?"; }

void pcs_init(pcs_sm_t *sm, uint32_t now_ms)
{
    memset(sm, 0, sizeof(*sm));
    sm->state = PCS_UNKNOWN;
    sm->cand = PCS_UNKNOWN;
    sm->cand_since = now_ms;
    sm->boot_ms = now_ms;
    sm->up_edge_ms = now_ms;
}

static uint32_t hold_ms(pcs_t s)
{
    switch (s) {
    case PCS_OFF: return PCS_HOLD_OFF_MS;
    case PCS_SLEEP: return PCS_HOLD_SLEEP_MS;
    case PCS_ON_NO_AGENT: return PCS_HOLD_NOAGENT_MS;
    default: return 0;
    }
}

pcs_t pcs_raw(const pcs_sm_t *sm, const pcs_inputs_t *in, uint32_t now_ms)
{
    bool up = in->mounted && !in->suspended;
    if (in->agent_online) return PCS_ON;
    if (in->wake_in_progress && !up) return PCS_BOOTING;
    /* An acked shutdown holds until an OS is back (agent ready / CDC port opened): in S5
     * a powered port is re-enumerated by the BIOS/EC and the bus can stay up for hours. */
    if (in->shutdown_expected) return in->wake_in_progress ? PCS_BOOTING : PCS_OFF;
    if (in->mounted && in->suspended) return PCS_SLEEP;
    if (!in->mounted) return PCS_OFF;
    if (sm->agent_since_up) return PCS_ON_NO_AGENT;
    if (ELAPSED(now_ms, sm->up_edge_ms) < PCS_BOOT_GRACE_MS) return PCS_BOOTING;
    return PCS_ON_NO_AGENT;
}

bool pcs_update(pcs_sm_t *sm, const pcs_inputs_t *in, uint32_t now_ms)
{
    /* edge detection */
    if (sm->have_prev) {
        bool was_up = sm->prev.mounted && !sm->prev.suspended;
        bool is_up = in->mounted && !in->suspended;
        if ((!was_up && is_up) || (in->wake_in_progress && !sm->prev.wake_in_progress)) {
            sm->up_edge_ms = now_ms;
            sm->agent_since_up = false;
        }
    } else {
        /* First sample: if the bus is already up at boot we do not know for how long:
         * treat the dongle boot as the up-edge (BOOTING until the grace ends). */
        sm->up_edge_ms = sm->boot_ms;
    }
    if (in->agent_online) {
        sm->agent_since_up = true;
        sm->agent_lost = false;
    }
    /* agent_lost: the agent disappeared while the PC is still up */
    bool is_up = in->mounted && !in->suspended;
    if (!is_up || in->shutdown_expected) sm->agent_lost = false;
    else if (sm->have_prev && sm->prev.agent_online && !in->agent_online) sm->agent_lost = true;
    sm->prev = *in;
    sm->have_prev = true;

    pcs_t raw = pcs_raw(sm, in, now_ms);
    if (sm->state == PCS_UNKNOWN && ELAPSED(now_ms, sm->boot_ms) < PCS_STARTUP_SETTLE_MS && raw != PCS_ON) {
        sm->cand = raw;
        sm->cand_since = now_ms;
        return false;
    }
    if (raw == sm->state) {
        sm->cand = raw;
        return false;
    }
    if (raw != sm->cand) {
        sm->cand = raw;
        sm->cand_since = now_ms;
    }
    uint32_t h = hold_ms(raw);
    /* leaving UNKNOWN after the settle period only needs the raw state */
    if (sm->state == PCS_UNKNOWN) h = 0;
    if ((uint32_t)ELAPSED(now_ms, sm->cand_since) >= h) {
        sm->state = raw;
        sm->changes++;
        return true;
    }
    return false;
}
