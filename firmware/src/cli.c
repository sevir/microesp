/*
 * MicroESP — service CLI on the CDC port. Lines starting with '!' are CLI; protocol
 * lines start with '{'. Handled by the HAL directly (work even if the app hangs):
 * !dfu !usj !log !reboot. Everything else is handled here, in the app task.
 *
 * Access policy (core/cli_policy.c): anyone with access to the port (dialout/root)
 * can type here, so a RELEASE build (MESP_DEV_CLI=n) only accepts read-only/recovery
 * commands; !auth/!pid only while that item is not provisioned yet or within the
 * 120 s provisioning window opened by holding the button 5..10 s. A development build
 * ("./build.sh dev") accepts everything. Secrets (Tuya AuthKey, agent key, pairing
 * code) are never printed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli_policy.h"
#include "mesp_hal.h"
#include "modules.h"
#include "tal_api.h"

#define OUT(...) mhal_cdc_printf(__VA_ARGS__)

static clip_window_t s_prov;

void cli_prov_window_open(void)
{
    clip_window_open(&s_prov, app_now_ms());
    PR_NOTICE("provisioning window open for %d s (!auth / !pid)", CLIP_PROV_WINDOW_MS / 1000);
    app_toast("Aprovisionar 120 s", 5000);
}

int cli_prov_window_remaining_s(void)
{
    uint32_t now = app_now_ms();
    return clip_window_active(&s_prov, now) ? clip_window_remaining_s(&s_prov, now) : 0;
}

static struct {
    bool armed;
    uint32_t at;
    pwr_action_t action;
    int countdown;
} s_sched;

static void cmd_help(void)
{
    OUT("MicroESP %s CLI, %s build (protocol lines start with '{'):\r\n", MESP_FW_VERSION, MESP_BUILD_FLAVOUR);
    OUT("  !status !version !dp !log !help !cancel\r\n");
    OUT("  !auth <uuid> <authkey>   !pid <pid>   (Tuya credentials -> NVS, apply with !reboot)\r\n");
#if MESP_DEV
    OUT("  !auth clear              (erase the Tuya credentials/PID stored in NVS)\r\n");
#else
    OUT("    release: only while not provisioned, or 120 s after holding the button 5 s (now %s)\r\n",
        cli_prov_window_remaining_s() ? "OPEN" : "closed");
    OUT("  !reboot !dfu !usj        (restart / ROM download mode / one boot without TinyUSB)\r\n");
    return;
#endif
    OUT("  !pair  !unpair           (agent pairing mode / forget key)\r\n");
    OUT("  !wake [force]            (dry run unless 'force'; ignored while the PC is on)\r\n");
    OUT("  !method <hid|wol|hid_then_wol>   !countdown <0..60>\r\n");
    OUT("  !cmd <shutdown|reboot> [countdown_s] [delay_s]  (simulates DP 103/104)   !cancel\r\n");
    OUT("  !key                     (harmless Left-Shift tap)\r\n");
    OUT("  !reset-tuya              (unbind from Tuya, back to BLE/AP provisioning)\r\n");
    OUT("  !reboot !dfu !usj        (restart / ROM download mode / one boot without TinyUSB)\r\n");
}

static void cmd_status(void)
{
    uint32_t now = app_now_ms();
    link_t *l = &g_app.link;
    OUT("fw=%s tuyaopen=%s uptime=%lus reset=%s ota_part=%s image=%s rollback=%s\r\n", MESP_FW_VERSION, OPEN_VERSION,
        (unsigned long)mhal_uptime_s(), mhal_reset_reason(), mhal_ota_running(), ota_status(),
        mhal_ota_rollback_enabled() ? "on" : "off");
    OUT("usb: mode=%d mounted=%d suspended=%d rwu_armed=%d cdc_open=%d kbd_leds=0x%02x events=%lu crash_count=%lu "
        "serial=%s\r\n",
        mhal_usb_mode(), mhal_usb_mounted(), mhal_usb_suspended(), mhal_usb_rwu_enabled(), mhal_cdc_connected(),
        mhal_kbd_leds(), (unsigned long)g_app.usb_events, (unsigned long)mhal_crash_count(), mhal_usb_serial());
    OUT("pc_state=%s faults=0x%02lx%s%s%s%s\r\n", pcs_name(g_app.pcs.state), (unsigned long)g_app.faults,
        g_app.faults & FAULT_AGENT_LOST ? " agent_lost" : "", g_app.faults & FAULT_WAKE_FAILED ? " wake_failed" : "",
        g_app.faults & FAULT_HID_NOT_ARMED ? " hid_not_armed" : "",
        g_app.faults & FAULT_CLOUD_LOST ? " cloud_lost" : "");
    OUT("agent: key=%s session=%s agent_online=%d sessions=%lu rx_lines=%lu rx_bad=%lu host=%s macs=%d\r\n",
        l->has_key ? "yes" : "no",
        l->st == LINK_READY ? "ready" : l->st == LINK_WAIT_AUTH ? "wait_auth" : "idle", link_online(l),
        (unsigned long)l->sessions, (unsigned long)l->rx_lines, (unsigned long)l->rx_bad,
        g_app.hostname[0] ? g_app.hostname : "-", g_app.nmacs);
    if (g_app.cpu >= 0)
        OUT("tele: count=%lu cpu=%d.%d%% mem=%d.%d%% disk_free=%d.%d%% pc_uptime=%lus\r\n",
            (unsigned long)g_app.tele_count, g_app.cpu / 10, g_app.cpu % 10, g_app.mem / 10, g_app.mem % 10,
            g_app.disk_free / 10, g_app.disk_free % 10, (unsigned long)g_app.pc_uptime);
    else
        OUT("tele: none yet\r\n");
    OUT("power: state=%s action=%s remaining=%ds countdown_setting=%ds last_result=%s\r\n",
        g_app.pwr.st == PWR_IDLE ? "idle" : g_app.pwr.st == PWR_COUNTDOWN ? "countdown" : "wait_ack",
        pwr_action_name(g_app.pwr.action), pwr_remaining_s(&g_app.pwr, now), g_app.countdown_s,
        g_app.have_last_result ? lr_name(g_app.last_result) : "-");
    if (s_sched.armed)
        OUT("scheduled: %s countdown=%ds in %lds\r\n", pwr_action_name(s_sched.action), s_sched.countdown,
            (long)((int32_t)(s_sched.at - now) / 1000));
    OUT("wake: method=%s active=%d attempts=%lu ok=%lu failed=%lu\r\n", wake_method_name(g_app.wake_method),
        g_app.wake.active, (unsigned long)g_app.wake.attempts, (unsigned long)g_app.wake.successes,
        (unsigned long)g_app.wake.failures);
    OUT("pairing: active=%d remaining=%ds (code on the display only)\r\n", pairing_active(), pairing_remaining_s());
    OUT("cli: build=%s provisioning_window=%ds tuya_creds=%s tuya_pid=%s\r\n", MESP_BUILD_FLAVOUR,
        cli_prov_window_remaining_s(), tuya_dp_creds_provisioned() ? "stored" : "none",
        tuya_dp_pid_provisioned() ? "stored" : "none");
    char ip[16] = "-";
    mhal_ip(ip);
    OUT("tuya: pid=%s uuid=%.6s... cred_src=%s activated=%d wifi=%d ip=%s mqtt=%d dp_reports=%lu ota=%s\r\n",
        tuya_dp_pid(), tuya_dp_uuid(), tuya_dp_cred_source(), g_app.activated, g_app.wifi_up, ip,
        g_app.cloud_connected, (unsigned long)g_app.dpm.total_reports, g_app.ota_running ? g_app.ota_version : "-");
    OUT("heap: internal free=%lu min=%lu psram free=%lu\r\n", (unsigned long)mhal_heap_internal_free(),
        (unsigned long)mhal_heap_internal_min(), (unsigned long)mhal_psram_free());
}

static int split(char *s, char **argv, int max)
{
    int n = 0;
    for (char *t = strtok(s, " \t"); t && n < max; t = strtok(NULL, " \t")) argv[n++] = t;
    return n;
}

void cli_handle(const char *line)
{
    char buf[160];
    snprintf(buf, sizeof(buf), "%s", line);
    char *argv[6];
    int argc = split(buf, argv, 6);
    if (!argc) return;
    const char *c = argv[0];
    uint32_t now = app_now_ms();
    clip_ctx_t pc = {.dev = MESP_DEV,
                     .creds_provisioned = tuya_dp_creds_provisioned(),
                     .pid_provisioned = tuya_dp_pid_provisioned(),
                     .prov_window = clip_window_active(&s_prov, now)};
    clip_rc_t acc = clip_check(c, &pc);
    PR_NOTICE("cli: %s%s", c, acc == CLIP_ALLOW ? "" : " (refused)"); /* never log arguments (credentials) */
    if (acc == CLIP_DEV_ONLY) {
        OUT("err: %s needs a development build (MESP_DEV_CLI, ./build.sh dev)\r\n", c);
        return;
    }
    if (acc == CLIP_LOCKED) {
        OUT("err: %s is locked (already provisioned): hold the button 5 s to open a 120 s provisioning window\r\n", c);
        return;
    }
    if (!strcmp(c, "!help")) {
        cmd_help();
    } else if (!strcmp(c, "!status")) {
        cmd_status();
    } else if (!strcmp(c, "!version")) {
        OUT("%s\r\n", MESP_FW_VERSION);
    } else if (!strcmp(c, "!dp")) {
        char j[512];
        dpm_to_json(&g_app.dpm, j, sizeof(j));
        OUT("%s\r\n", j);
    } else if (!strcmp(c, "!auth") && argc == 2 && !strcmp(argv[1], "clear")) {
#if MESP_DEV
        OUT(tuya_dp_clear_creds() == 0 ? "ok: Tuya UUID/AuthKey/PID erased from NVS, apply with !reboot\r\n"
                                       : "err: NVS erase failed\r\n");
#else
        OUT("err: !auth clear needs a development build\r\n");
#endif
    } else if (!strcmp(c, "!auth")) {
        int rc = argc == 3 ? tuya_dp_set_creds(argv[1], argv[2]) : -1;
        memset(buf, 0, sizeof(buf)); /* do not leave the AuthKey on the stack */
        OUT(rc == 0 ? "ok: credentials stored in NVS, apply with !reboot\r\n"
                    : rc == -1 ? "err: usage !auth <uuid> <authkey> (alphanumeric)\r\n" : "err: NVS write failed\r\n");
    } else if (!strcmp(c, "!pid")) {
        int rc = argc == 2 ? tuya_dp_set_pid(argv[1]) : -1;
        OUT(rc == 0 ? "ok: product id stored in NVS, apply with !reboot\r\n"
                    : rc == -1 ? "err: usage !pid <pid> (alphanumeric)\r\n" : "err: NVS write failed\r\n");
    } else if (!strcmp(c, "!pair")) {
        pairing_start("cli");
        OUT("ok: agent pairing mode for %d s; the code is on the display only\r\n", pairing_remaining_s());
    } else if (!strcmp(c, "!unpair")) {
        pairing_forget();
        OUT("ok: agent key forgotten\r\n");
    } else if (!strcmp(c, "!wake")) {
        wake_usb_t u = {usbc_mounted(), usbc_suspended(), usbc_rwu()};
        if (argc >= 2 && !strcmp(argv[1], "force")) {
            wake_rc_t rc = wake_power_on("cli");
            OUT("wake: %s\r\n", rc == WAKE_STARTED ? "started" : rc == WAKE_IGNORED_ON ? "ignored, PC is on"
                                : rc == WAKE_BUSY                              ? "busy"
                                                                               : "no target");
        } else {
            OUT("wake (dry run, method %s): %s\r\n", wake_method_name(g_app.wake_method),
                wake_plan(g_app.wake_method, &u));
        }
    } else if (!strcmp(c, "!method")) {
        int m = argc == 2 ? wake_method_parse(argv[1]) : -1;
        if (m < 0) OUT("err: usage !method <hid|wol|hid_then_wol>\r\n");
        else {
            wake_set_method((wake_method_t)m);
            OUT("ok: wake_method=%s\r\n", argv[1]);
        }
    } else if (!strcmp(c, "!countdown")) {
        int s = argc == 2 ? atoi(argv[1]) : -1;
        if (s < 0 || s > 60) OUT("err: usage !countdown <0..60>\r\n");
        else {
            power_set_countdown(s);
            OUT("ok: cmd_countdown=%d\r\n", s);
        }
    } else if (!strcmp(c, "!cmd")) {
        pwr_action_t a = argc >= 2 && !strcmp(argv[1], "shutdown") ? PWR_SHUTDOWN
                         : argc >= 2 && !strcmp(argv[1], "reboot") ? PWR_REBOOT
                                                                   : PWR_NONE;
        int cd = argc >= 3 ? atoi(argv[2]) : g_app.countdown_s;
        int delay = argc >= 4 ? atoi(argv[3]) : 0;
        if (a == PWR_NONE || cd < 0 || cd > 60 || delay < 0 || delay > 600) {
            OUT("err: usage !cmd <shutdown|reboot> [countdown 0..60] [delay 0..600 s]\r\n");
        } else {
            s_sched.armed = true;
            s_sched.at = app_now_ms() + (uint32_t)delay * 1000u;
            s_sched.action = a;
            s_sched.countdown = cd;
            OUT("ok: %s with countdown %d s scheduled in %d s (as if DP %d were set)\r\n", pwr_action_name(a), cd,
                delay, a == PWR_SHUTDOWN ? 103 : 104);
        }
    } else if (!strcmp(c, "!cancel")) {
        bool sch = s_sched.armed;
        s_sched.armed = false;
        OUT(power_cancel("cli") ? "ok: countdown cancelled\r\n"
                                : sch ? "ok: scheduled cmd dropped\r\n" : "nothing to cancel\r\n");
    } else if (!strcmp(c, "!key")) {
        OUT(mhal_hid_tap(0x02, 0) == 0 ? "ok: left shift tapped\r\n" : "err: HID not ready (bus down/suspended)\r\n");
    } else if (!strcmp(c, "!reset-tuya")) {
        OUT("ok: Tuya reset requested\r\n");
        tuya_dp_factory_reset("cli");
    } else {
        OUT("err: unknown command %s (try !help)\r\n", c);
    }
}

void cli_tick(uint32_t now)
{
    if (!s_sched.armed || (int32_t)(now - s_sched.at) < 0) return;
    s_sched.armed = false;
    int saved = g_app.countdown_s;
    g_app.countdown_s = s_sched.countdown; /* one-shot override, not persisted */
    power_request(s_sched.action, "cli !cmd");
    g_app.countdown_s = saved;
}
