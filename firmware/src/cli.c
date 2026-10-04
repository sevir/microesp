/*
 * MicroESP — service CLI on the CDC port. Lines starting with '!' are CLI; protocol
 * lines start with '{'. Handled by the HAL directly (work even if the app hangs):
 * !dfu !usj !log !reboot. Everything else is handled here, in the app task.
 *
 * Access policy (core/cli_policy.c): anyone with access to the port (dialout/root)
 * can type here, so a RELEASE build (MESP_DEV_CLI=n) only accepts read-only/recovery
 * commands; !tylink/!wifi only while that item is not provisioned yet or within the
 * 120 s provisioning window opened by holding the button 5..10 s. A development build
 * ("./build.sh dev") accepts everything. Secrets (TuyaLink deviceSecret, Wi-Fi
 * password, agent key, pairing code) are never printed nor logged: a CLI line is
 * logged by its command name only.
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
    PR_NOTICE("provisioning window open for %d s (!tylink / !wifi)", CLIP_PROV_WINDOW_MS / 1000);
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
    OUT("  !tylink <eu|us|cn|in> <productId> <deviceId> <deviceSecret>   (TuyaLink -> NVS, apply with !reboot)\r\n");
    OUT("  !wifi <ssid> <password...>   (password = rest of the line, may contain spaces; apply with !reboot)\r\n");
#if MESP_DEV
    OUT("  !tylink clear  !wifi clear   (erase them from NVS)\r\n");
#else
    OUT("    release: only while not provisioned, or 120 s after holding the button 5 s (now %s)\r\n",
        cli_prov_window_remaining_s() ? "OPEN" : "closed");
    OUT("  !reboot !dfu !usj        (restart / ROM download mode / one boot without TinyUSB)\r\n");
    return;
#endif
    OUT("  !pair  !unpair           (agent pairing mode / forget key)\r\n");
    OUT("  !wake [force]            (dry run unless 'force'; always sent, Alt+P if bus up)\r\n");
    OUT("  !method <hid|wol|hid_then_wol>   !countdown <0..60>\r\n");
    OUT("  !cmd <shutdown|reboot> [countdown_s] [delay_s]  (simulates DP 103/104)   !cancel\r\n");
    OUT("  !key                     (harmless Left-Shift tap)\r\n");
    OUT("  !reboot !dfu !usj        (restart / ROM download mode / one boot without TinyUSB)\r\n");
}

static void cmd_status(void)
{
    uint32_t now = app_now_ms();
    link_t *l = &g_app.link;
    OUT("fw=%s cloud=tuyalink tuyaopen=%s uptime=%lus reset=%s ota_part=%s image=%s rollback=%s\r\n", MESP_FW_VERSION, OPEN_VERSION,
        (unsigned long)mhal_uptime_s(), mhal_reset_reason(), mhal_ota_running(), ota_status(),
        mhal_ota_rollback_enabled() ? "on" : "off");
    OUT("usb: mode=%d mounted=%d suspended=%d rwu_armed=%d cdc_open=%d kbd_leds=0x%02x hid_proto=%s events=%lu "
        "crash_count=%lu serial=%s\r\n",
        mhal_usb_mode(), mhal_usb_mounted(), mhal_usb_suspended(), mhal_usb_rwu_enabled(), mhal_cdc_connected(),
        mhal_kbd_leds(), mhal_hid_protocol() == 0 ? "boot" : "report", (unsigned long)g_app.usb_events,
        (unsigned long)mhal_crash_count(), mhal_usb_serial());
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
    OUT("cli: build=%s provisioning_window=%ds tylink_nvs=%s wifi_nvs=%s\r\n", MESP_BUILD_FLAVOUR,
        cli_prov_window_remaining_s(), cloud_tylink_provisioned() ? "stored" : "none",
        cloud_wifi_provisioned() ? "stored" : "none");
    cloud_print_status();
    OUT("heap: internal free=%lu min=%lu psram free=%lu\r\n", (unsigned long)mhal_heap_internal_free(),
        (unsigned long)mhal_heap_internal_min(), (unsigned long)mhal_psram_free());
}

static int split(char *s, char **argv, int max)
{
    int n = 0;
    for (char *t = strtok(s, " \t"); t && n < max; t = strtok(NULL, " \t")) argv[n++] = t;
    return n;
}

static void cmd_tylink(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "clear")) {
#if MESP_DEV
        OUT(cloud_clear_tylink() == 0 ? "ok: TuyaLink settings erased from NVS, apply with !reboot\r\n"
                                      : "err: NVS erase failed\r\n");
#else
        OUT("err: !tylink clear needs a development build\r\n");
#endif
        return;
    }
    int rc = argc == 5 ? cloud_set_tylink(argv[1], argv[2], argv[3], argv[4]) : -1;
    OUT(rc == 0    ? "ok: TuyaLink settings stored in NVS, apply with !reboot\r\n"
        : rc == -1 ? "err: usage !tylink <eu|us|cn|in> <productId> <deviceId> <deviceSecret> (ids alphanumeric 8..32,"
                     " secret 8..64 printable, no spaces)\r\n"
                   : "err: NVS write failed\r\n");
}

static void cmd_wifi(const char *line, int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "clear")) {
#if MESP_DEV
        OUT(cloud_clear_wifi() == 0 ? "ok: Wi-Fi settings erased from NVS, apply with !reboot\r\n"
                                    : "err: NVS erase failed\r\n");
#else
        OUT("err: !wifi clear needs a development build\r\n");
#endif
        return;
    }
    char ssid[CLIP_SSID_MAX + 1], pass[CLIP_WIFI_PASS_MAX + 1];
    int rc = clip_parse_wifi(line, ssid, pass) == 0 ? cloud_set_wifi(ssid, pass) : -1;
    memset(pass, 0, sizeof(pass));
    OUT(rc == 0    ? "ok: Wi-Fi settings stored in NVS, apply with !reboot\r\n"
        : rc == -1 ? "err: usage !wifi <ssid> <password...> (SSID without spaces, 1..32 bytes; password 8..64 "
                     "chars, may contain spaces)\r\n"
                   : "err: NVS write failed\r\n");
}

void cli_handle(const char *line)
{
    char buf[256];
    if (strlen(line) >= sizeof(buf)) {
        OUT("err: line too long\r\n");
        return;
    }
    snprintf(buf, sizeof(buf), "%s", line);
    char *argv[6];
    int argc = split(buf, argv, 6);
    if (!argc) return;
    const char *c = argv[0];
    uint32_t now = app_now_ms();
    clip_ctx_t pc = {.dev = MESP_DEV,
                     .tylink_provisioned = cloud_tylink_provisioned(),
                     .wifi_provisioned = cloud_wifi_provisioned(),
                     .prov_window = clip_window_active(&s_prov, now)};
    clip_rc_t acc = clip_check(c, &pc);
    PR_NOTICE("cli: %s%s", c, acc == CLIP_ALLOW ? "" : " (refused)"); /* never log arguments (credentials) */
    if (acc == CLIP_OBSOLETE) {
        OUT("err: %s is not used with TuyaLink (use !tylink and !wifi)\r\n", c);
        return;
    }
    if (acc == CLIP_DEV_ONLY) {
        OUT("err: %s needs a development build (MESP_DEV_CLI, ./build.sh dev)\r\n", c);
        return;
    }
    if (acc == CLIP_LOCKED) {
        OUT("err: %s is locked (already provisioned): hold the button 5 s to open a 120 s provisioning window\r\n", c);
        return;
    }
    if (!strcmp(c, "!tylink") || !strcmp(c, "!wifi")) {
        if (!strcmp(c, "!tylink")) cmd_tylink(argc, argv);
        else cmd_wifi(line, argc, argv);
        memset(buf, 0, sizeof(buf)); /* do not leave secrets on the stack */
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
    } else if (!strcmp(c, "!pair")) {
        pairing_start("cli");
        OUT("ok: agent pairing mode for %d s; the code is on the display only\r\n", pairing_remaining_s());
    } else if (!strcmp(c, "!unpair")) {
        pairing_forget();
        OUT("ok: agent key forgotten\r\n");
    } else if (!strcmp(c, "!wake")) {
        wake_usb_t u = wake_usb_now();
        if (argc >= 2 && !strcmp(argv[1], "force")) {
            wake_rc_t rc = wake_power_on("cli");
            OUT("wake: %s\r\n", rc == WAKE_STARTED ? "started" : rc == WAKE_RESENT ? "re-sent" : "no target");
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
