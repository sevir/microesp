/*
 * MicroESP — agent link glue (US-0022): connects the pure-C cdc-v1 session
 * (core/link_proto.c) to the CDC port, NVS, DPs and the power-command and script flows.
 *
 * Lines starting with '!' are CLI (cli.c); every other line is protocol.
 * The session is dropped as soon as the host closes the port (DTR low).
 * Hello data (hostname, MACs for WOL) is only trusted/stored once the session is
 * authenticated (ready).
 */
#include <stdio.h>
#include <string.h>

#include "mesp_hal.h"
#include "modules.h"
#include "tal_api.h"

#define NVS_AGENT_KEY "agent_key"

static void cb_send(void *ctx, const char *line)
{
    char buf[LINK_MAX_LINE + 2];
    int n = snprintf(buf, sizeof(buf), "%s\n", line);
    if (n > 0 && n < (int)sizeof(buf)) mhal_cdc_write(buf, (size_t)n);
}

static void cb_random(void *ctx, uint8_t *buf, size_t n) { mhal_random(buf, n); }

static bool cb_save_key(void *ctx, const uint8_t key[MC_KEY_LEN])
{
    return mhal_nvs_set_blob(NVS_AGENT_KEY, key, MC_KEY_LEN) == 0;
}

static void cb_event(void *ctx, const link_ev_t *ev)
{
    switch (ev->type) {
    case LINK_EV_HELLO:
        snprintf(g_app.hostname, sizeof(g_app.hostname), "%s", ev->hello.host);
        dpm_set_str(&g_app.dpm, DP_PC_HOSTNAME, ev->hello.host);
        wake_store_macs(ev->hello.macs, ev->hello.nmacs);
        PR_NOTICE("agent: host=%s macs=%d", ev->hello.host, ev->hello.nmacs);
        break;
    case LINK_EV_READY:
        PR_NOTICE("agent: session ready (#%lu)", (unsigned long)g_app.link.sessions);
        /* an authenticated agent is running again: a previously acked shutdown did not
         * happen (aborted), so a later bus suspend is a real sleep, not "off" */
        g_app.shutdown_expected = false;
        break;
    case LINK_EV_ONLINE:
        PR_NOTICE("agent: online=%d", ev->online);
        dpm_set(&g_app.dpm, DP_AGENT_ONLINE, ev->online);
        break;
    case LINK_EV_TELE:
        g_app.cpu = ev->tele.cpu;
        g_app.mem = ev->tele.mem;
        g_app.disk_free = ev->tele.disk_free;
        g_app.pc_uptime = ev->tele.uptime;
        g_app.tele_count++;
        dpm_set(&g_app.dpm, DP_CPU, ev->tele.cpu);
        dpm_set(&g_app.dpm, DP_MEM, ev->tele.mem);
        dpm_set(&g_app.dpm, DP_DISK_FREE, ev->tele.disk_free);
        dpm_set(&g_app.dpm, DP_PC_UPTIME, (int32_t)(ev->tele.uptime > 999999999u ? 999999999u : ev->tele.uptime));
        break;
    case LINK_EV_ACK:
        PR_NOTICE("agent: ack id=%lu ok=%d err=%s", (unsigned long)ev->ack.id, ev->ack.ok, ev->ack.err);
        /* only the ack of the shutdown cmd itself (a script ack may arrive meanwhile) */
        if (ev->ack.ok && g_app.pwr.st == PWR_WAIT_ACK && g_app.pwr.action == PWR_SHUTDOWN &&
            ev->ack.id == g_app.pwr.cmd_id)
            g_app.shutdown_expected = true;
        pwr_on_ack(&g_app.pwr, ev->ack.id, ev->ack.ok);
        scr_on_ack(&g_app.scr, ev->ack.id, ev->ack.ok);
        break;
    case LINK_EV_ACK_TIMEOUT:
        PR_WARN("agent: no ack for cmd id=%lu (%s)", (unsigned long)ev->ack.id, ev->ack.err);
        pwr_on_ack_timeout(&g_app.pwr, ev->ack.id);
        scr_on_ack_timeout(&g_app.scr, ev->ack.id);
        break;
    case LINK_EV_PAIRED:
        PR_NOTICE("agent: PAIRED, new key stored");
        app_toast("Agente emparejado", 3000);
        break;
    case LINK_EV_PAIR_FAIL:
        PR_WARN("agent: pairing attempt failed (%d/%d)", ev->pair_fails, LINK_PAIR_MAX_FAILS);
        app_toast("Codigo incorrecto", 2000);
        break;
    case LINK_EV_PAIR_END:
        pairing_on_end();
        break;
    case LINK_EV_SCRIPTS:
        scripts_on_list();
        break;
    case LINK_EV_PROTO_ERR:
        PR_DEBUG("agent: sent err %s", ev->err_code);
        break;
    }
}

void agent_link_init(void)
{
    uint8_t key[MC_KEY_LEN];
    size_t len = sizeof(key);
    bool has = mhal_nvs_get_blob(NVS_AGENT_KEY, key, &len) == 0 && len == MC_KEY_LEN;
    char dev[13];
    mhal_mac_hex(dev);
    link_cbs_t cb = {.send = cb_send, .random = cb_random, .save_key = cb_save_key, .event = cb_event};
    link_init(&g_app.link, &cb, MESP_FW_VERSION, dev, has ? key : NULL);
    memset(key, 0, sizeof(key));
    dpm_set(&g_app.dpm, DP_AGENT_ONLINE, 0);
    PR_NOTICE("agent link: dev=%s key=%s", dev, has ? "present" : "NONE (pairing needed)");
}

void agent_link_on_line(const char *line, size_t len, uint32_t now) { link_rx_line(&g_app.link, line, len, now); }

void agent_link_on_too_long(uint32_t now) { link_rx_too_long(&g_app.link, now); }

void agent_link_tick(uint32_t now) { link_tick(&g_app.link, now); }

/* Port closed (agent stopped/crashed): the session ends now instead of after the
 * 15 s heartbeat timeout, and nobody can reuse it without a new handshake. */
void agent_link_on_port_closed(void)
{
    if (g_app.link.st != LINK_IDLE) PR_NOTICE("agent: port closed -> session dropped");
    link_close_session(&g_app.link);
}
