#include "scriptcmd.h"

#include <stdio.h>
#include <string.h>

void scr_init(scr_t *s, const scr_cbs_t *cb)
{
    memset(s, 0, sizeof(*s));
    if (cb) s->cb = *cb;
}

static void result(scr_t *s, const char *id, last_result_t r)
{
    if (r == LR_OK) s->ok++;
    else if (r == LR_AGENT_OFFLINE) s->offline++;
    else s->rejected++;
    if (s->cb.result) s->cb.result(s->cb.ctx, id, r);
}

bool scr_request(scr_t *s, const char *id, bool agent_ready, bool known)
{
    s->runs++;
    if (!id) id = "";
    if (!agent_ready) {
        result(s, id, LR_AGENT_OFFLINE);
        return false;
    }
    /* one script cmd awaiting its ack at a time: a second request is rejected, the
     * first one keeps waiting */
    if (!known || s->waiting || !link_valid_script_id(id)) {
        result(s, id, LR_CMD_REJECTED);
        return false;
    }
    char action[LINK_ACTION_MAX + 1];
    snprintf(action, sizeof(action), LINK_SCRIPT_PREFIX "%s", id);
    uint32_t cmd = s->cb.send_cmd ? s->cb.send_cmd(s->cb.ctx, action) : 0;
    if (!cmd) {
        result(s, id, LR_CMD_REJECTED);
        return false;
    }
    s->waiting = true;
    s->cmd_id = cmd;
    snprintf(s->id, sizeof(s->id), "%s", id);
    return true;
}

static void finish(scr_t *s, last_result_t r)
{
    char id[LINK_SCRIPT_ID_MAX + 1];
    snprintf(id, sizeof(id), "%s", s->id);
    s->waiting = false;
    s->cmd_id = 0;
    s->id[0] = 0;
    result(s, id, r);
}

void scr_on_ack(scr_t *s, uint32_t cmd_id, bool ok)
{
    if (!s->waiting || cmd_id != s->cmd_id) return;
    finish(s, ok ? LR_OK : LR_CMD_REJECTED);
}

void scr_on_ack_timeout(scr_t *s, uint32_t cmd_id)
{
    if (!s->waiting || cmd_id != s->cmd_id) return;
    finish(s, LR_CMD_REJECTED);
}
