#include "powercmd.h"

#include <string.h>

#define ELAPSED(now, t) ((int32_t)((uint32_t)(now) - (uint32_t)(t)))

static const char *const k_lr[LR__COUNT] = {"ok", "wake_sent", "wake_failed", "cmd_rejected", "agent_offline",
                                             "cancelled"};

const char *lr_name(last_result_t r) { return (unsigned)r < LR__COUNT ? k_lr[r] : "?"; }

const char *pwr_action_name(pwr_action_t a)
{
    return a == PWR_SHUTDOWN ? "shutdown" : a == PWR_REBOOT ? "reboot" : "none";
}

void pwr_init(pwr_t *p, const pwr_cbs_t *cb)
{
    memset(p, 0, sizeof(*p));
    if (cb) p->cb = *cb;
}

static void finish(pwr_t *p, last_result_t r)
{
    pwr_action_t a = p->action;
    p->st = PWR_IDLE;
    p->action = PWR_NONE;
    p->cmd_id = 0;
    if (p->cb.result) p->cb.result(p->cb.ctx, a, r);
    if (p->cb.reset_dps) p->cb.reset_dps(p->cb.ctx);
}

static void fire(pwr_t *p, bool agent_online)
{
    if (!agent_online) { finish(p, LR_AGENT_OFFLINE); return; }
    uint32_t id = p->cb.send_cmd ? p->cb.send_cmd(p->cb.ctx, pwr_action_name(p->action)) : 0;
    if (!id) { finish(p, LR_AGENT_OFFLINE); return; }
    p->cmd_id = id;
    p->st = PWR_WAIT_ACK;
}

bool pwr_request(pwr_t *p, pwr_action_t a, int countdown_s, bool agent_online, uint32_t now_ms)
{
    if (a != PWR_SHUTDOWN && a != PWR_REBOOT) return false;
    if (p->st != PWR_IDLE) return false;
    p->action = a;
    if (!agent_online) {
        finish(p, LR_AGENT_OFFLINE);
        return true;
    }
    if (countdown_s < 0) countdown_s = 0;
    if (countdown_s > 60) countdown_s = 60;
    if (countdown_s == 0) {
        fire(p, agent_online);
        return true;
    }
    p->st = PWR_COUNTDOWN;
    p->deadline_ms = now_ms + (uint32_t)countdown_s * 1000u;
    if (p->cb.send_notice) p->cb.send_notice(p->cb.ctx, pwr_action_name(a), countdown_s);
    return true;
}

bool pwr_cancel(pwr_t *p)
{
    if (p->st != PWR_COUNTDOWN) return false;
    if (p->cb.send_notice) p->cb.send_notice(p->cb.ctx, "cancel", 0);
    finish(p, LR_CANCELLED);
    return true;
}

void pwr_tick(pwr_t *p, bool agent_online, uint32_t now_ms)
{
    if (p->st == PWR_COUNTDOWN && ELAPSED(now_ms, p->deadline_ms) >= 0) fire(p, agent_online);
}

void pwr_on_ack(pwr_t *p, uint32_t id, bool ok)
{
    if (p->st != PWR_WAIT_ACK || id != p->cmd_id) return;
    finish(p, ok ? LR_OK : LR_CMD_REJECTED);
}

void pwr_on_ack_timeout(pwr_t *p, uint32_t id)
{
    if (p->st != PWR_WAIT_ACK || id != p->cmd_id) return;
    finish(p, LR_CMD_REJECTED);
}

int pwr_remaining_s(const pwr_t *p, uint32_t now_ms)
{
    if (p->st != PWR_COUNTDOWN) return 0;
    int32_t ms = ELAPSED(p->deadline_ms, now_ms);
    return ms <= 0 ? 0 : (int)((ms + 999) / 1000);
}
