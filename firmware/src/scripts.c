/*
 * MicroESP — user scripts (cdc-v1 §3.1, DP 115 scripts / DP 116 script_run).
 * Flow: core/scriptcmd.c. The list lives in g_app.link.scripts (RAM only, replaced by
 * each valid scripts{list} of the agent, kept when the session drops: the panel hides
 * it while agent_online is false). DP 115 stays unreported until the agent sent its
 * list once. DP 116 is a push button: a cloud write runs that script and the DP is
 * reported back to "".
 */
#include <stdio.h>
#include <string.h>

#include "modules.h"
#include "tal_api.h"

static uint32_t cb_cmd(void *c, const char *action)
{
    uint32_t id = link_send_cmd(&g_app.link, action, app_now_ms());
    PR_NOTICE("scripts: cmd %s -> id %lu", action, (unsigned long)id);
    return id;
}

static void cb_result(void *c, const char *script_id, last_result_t r)
{
    PR_NOTICE("scripts: %s finished: %s", script_id, lr_name(r));
    app_set_last_result(r);
    app_toast(r == LR_OK ? "Script lanzado" : lr_name(r), 3000);
}

void scripts_init(void)
{
    scr_cbs_t cb = {cb_cmd, cb_result, NULL};
    scr_init(&g_app.scr, &cb);
    dpm_set_str(&g_app.dpm, DP_SCRIPT_RUN, "");
}

void scripts_on_list(void)
{
    char json[LINK_SCRIPTS_JSON_MAX + 1];
    if (link_scripts_json(&g_app.link, json, sizeof(json)) < 0 || !dpm_set_str(&g_app.dpm, DP_SCRIPTS, json)) {
        PR_ERR("scripts: list does not fit DP 115, ignored");
        return;
    }
    PR_NOTICE("scripts: %d from the agent", g_app.link.nscripts);
}

void scripts_run(const char *id, const char *source)
{
    bool ready = link_ready(&g_app.link) && link_online(&g_app.link);
    bool known = link_script_find(&g_app.link, id) != NULL;
    PR_NOTICE("scripts: run '%s' requested by %s (agent ready=%d, known=%d)", id, source, ready, known);
    scr_request(&g_app.scr, id, ready, known);
}
