# E2E test plan v1 (Lenovo ThinkStation P3 Ultra SFF G2)

Story: MESP-US-0033. Acceptance criterion: all cases executed, with result and evidence, and **0 blocking failures**.

## Run data

| Field | Value |
|---|---|
| Date(s) | 2026-10-04 (partial) |
| Executed by | |
| Firmware version (`!version`) | 0.2.0 |
| Agent version (`microesp-agent version`) | f53ecc1 |
| Repository commit | e9cc1c8 |
| PC / BIOS version (`sudo dmidecode -s bios-version`) | ThinkStation P3 Ultra SFF G2 / |
| OS / kernel (`uname -r`) | |
| Dongle USB port | |
| BIOS settings applied ([`bios-lenovo.md`](../user/bios-lenovo.md) §2) | |
| NIC / WOL | `enp128s31f6` `fc:9d:05:18:ee:32`, `Wake-on:` |

**Result legend**: ✅ OK · ❌ Fails (state whether it is **blocking**) · ⚠️ OK with remarks · N/A not applicable.
**Evidence**: store it in `docs/qa/evidence/e2e-v1/` with the case id (for example `E2E-05-journal.txt`, `E2E-05-app.png`). It can be app screenshots, output of `!status`/`!dp`/`!log`, `journalctl -u microesp-agent` or photos of the screen.

**Safety**: run all shutdown and reboot tests first with `dry_run = true` in the agent, and close any open work on the PC before the real tests. The physical power button is always the recovery path.

## 1. Installation and pairing

| Case | Steps | Expected | Result | Evidence |
|---|---|---|---|---|
| E2E-01 Initial flashing | Flash the merged image ([installation §1](../user/installation.md#1-flash-the-firmware)); unplug and plug in again | Enumerates as `303a:4002` "MicroESP"; the screen shows the state; the LED blinks blue | OK | Flashed 0.2.0 over USB (tools/flash.sh); enumerates 303a:4002 `MESP-907069f662dc` |
| E2E-02 TuyaLink and Wi-Fi credentials | `!tylink <region> <productId> <deviceId> <deviceSecret>`, `!wifi <ssid> <password>`, `!reboot`; `!status` | `tylink: ... provisioned=1 mqtt=connected`, `wifi: ... up=1 ... time_synced=1`; no secrets in `!status` or `!log` | OK | `!status`: `wifi up=1`, `mqtt=connected` (eu), `time_synced=1`; secrets do not appear in `!log` |
| E2E-03 Device in Smart Life | Open the app (linked from the platform; no BLE) | The device appears online; properties `pc_state` … `fault` visible | OK | Linked by QR (+ → Scan) with the TuyaLink Central EU product; values visible after changing the panel |
| E2E-04 Agent installation | `sudo agent/deploy/install.sh` | Service active; `/dev/microesp` exists; `power/wakeup=enabled` on the dongle | OK | `install.sh --binary ./microesp-agent --no-start`; service active; starts by itself when the dongle re-enumerates |
| E2E-05 Agent ↔ dongle pairing | Button 3 s → code; `sudo microesp-agent pair` | `pair_ok`; `/etc/microesp/agent.key` exists (0600, `microesp`); after starting the service, `agent_online=true`, `pc_state=on` | OK | Code on screen (button 3-5 s); `sudo microesp-agent pair`; journal: `session ready` 02:06:32 |
| E2E-06 Pairing with wrong code | 3 incorrect codes | The dongle leaves pairing mode; the previous key does not change | | |
| E2E-07 Re-pairing | Pair again | The new key works on both sides; the previous one is invalidated | | |

## 2. Power-on

Before each case: the PC in the indicated state and the dongle connected to the cloud. Note the time from pressing in the app until `pc_state=on`.

| Case | Steps | Expected | Result | Evidence |
|---|---|---|---|---|
| E2E-10 Power-on from S3 (HID) | `sudo rtcwake -m mem -s 180`; `pc_state=sleep`; app → Power on (`wake_method=hid`) | Wakes before 180 s; `last_result=wake_sent` → `pc_state=on`; no `hid_not_armed` | | |
| E2E-11 Power-on from S3 (button) | Suspend; double press on the dongle button | Wakes | | |
| E2E-12 Power-on from S4 (HID) | `systemctl hibernate` (if configured); app → Power on | Wakes, or N/A if there is no hibernation | | |
| E2E-13 Power-on from S5 (HID) | BIOS *Smart Power On* enabled and dongle in the *smart power on* connector; `systemctl poweroff`; `wake_method=hid`; app → Power on | The dongle sends Alt+P and the PC boots. If not: `wake_failed` after 120 s; note whether the port supplies 5 V in S5 and `hid_proto` in `!status` | | |
| E2E-14 Power-on from S5 (WOL) | `wake_method=wol`; shut down; app → Power on | Boots via WOL | | |
| E2E-15 Power-on from S5 (`hid_then_wol`) | Default method; shut down; app → Power on | Alt+P and WOL are sent at the same time (WOL repeated after 20 s); boots | | |
| E2E-16 Power on with the PC already on | PC `on`; app → Power on | The command is sent anyway (Alt+P reaches the foreground application); with the agent connected it is accepted immediately; the PC is not affected | | |
| E2E-17 Power-on failure | Disable WOL on the NIC and shut down with the port unpowered; app → Power on | `wake_failed` and bit 1 of `fault` after 120 s; cleared on the next successful power-on | | |

## 3. Shutdown and reboot

| Case | Steps | Expected | Result | Evidence |
|---|---|---|---|---|
| E2E-20 Shutdown (dry-run) | `dry_run = true`; app → Shut down | Countdown on the screen (DP 112) and red LED; signed `cmd` → `ack ok`; log `dry-run`; `last_result=ok`; DP 103 returns to `false` | | |
| E2E-21 Real shutdown | `dry_run = false`; app → Shut down | The PC shuts down after the countdown; `pc_state` → `off` | | |
| E2E-22 Real reboot | app → Reboot | The PC reboots; the agent reconnects (`on`) | | |
| E2E-23 Cancel with the button | app → Shut down; short press during the countdown | `last_result=cancelled`; `notice{cancel}`; the PC stays on | OK (with Reboot) | journal 2026-10-04: `notice action=reboot in=10` 02:12:32 → `notice action=cancel` 02:12:35; PC stays on |
| E2E-24 Cancel from the app | app → Shut down; DP 103 to `false` during the countdown | `cancelled` | | |
| E2E-25 Shutdown without agent | `systemctl stop microesp-agent`; app → Shut down | `last_result=agent_offline`; no countdown | OK | Service stopped: app → Reboot → `last_result=agent_offline`, `rx_cmds=2` |
| E2E-26 Active inhibitor | `systemd-inhibit --what=shutdown sleep 600 &`; app → Shut down | `ack` and then `exec_failed` in the log, or `cmd_rejected`; the PC does **not** shut down | | |
| E2E-27 Command security | Review the tests (`go test ./internal/link`, `firmware/build.sh test`): wrong signature and replay | `bad_sig` / `replay` rejected (covered by tests) | | |

## 4. State and robustness

| Case | Steps | Expected | Result | Evidence |
|---|---|---|---|---|
| E2E-30 Agent down | `sudo systemctl stop microesp-agent` (or `kill -9`) | Within ≤ 15 s: `agent_online=false`, `pc_state=on_no_agent`, `fault` bit 0 `agent_lost`; amber LED | | |
| E2E-31 Agent recovered | `sudo systemctl start microesp-agent` | `on`, `agent_online=true`, `agent_lost` cleared | | |
| E2E-32 Dongle unplugged/replugged | Unplug for 10 s and plug in again with the PC on | The agent retries with backoff and reconnects by itself; `pc_state=on` in < 30 s | | |
| E2E-33 Wi-Fi loss | Turn the AP off for 2 min and back on | `cloud_lost` (bit 3) while it lasts; reconnects by itself; full DP report on reconnection | | |
| E2E-34 PC reboot | Reboot the PC from the OS | Sequence `off`/`booting` → `on`; the agent starts with the system | | |
| E2E-35 Suspend seen by the dongle | `systemctl suspend` | `pc_state=sleep` (≤ 3 s hysteresis) | | |
| E2E-36 Power outage | Remove power from the PC and restore it | Behavior according to "After Power Loss"; the dongle does not reset to Tuya | | |

## 5. Telemetry

| Case | Steps | Expected | Result | Evidence |
|---|---|---|---|---|
| E2E-40 CPU | Load with `stress-ng --cpu 0 -t 60s`; compare with `top`/`mpstat 5` | DP 105 within ±5 points | | |
| E2E-41 Memory | Compare with `free -m` (used, without cache or buffers) | DP 106 within ±2 points | | |
| E2E-42 Free disk | Compare with `df -h /` (and the configured disks) | DP 107 = lowest free %, ±1 point | | |
| E2E-43 Uptime and hostname | Compare with `uptime -p` and `hostname` | DP 110 (≤ 1 report/min) and DP 111 correct | | |
| E2E-44 MACs for WOL | `!status` (with the agent stopped) | `macs≥1`; includes `fc:9d:05:18:ee:32` | | |

## 6. OTA

| Case | Steps | Expected | Result | Evidence |
|---|---|---|---|---|
| E2E-50 Successful OTA | Upload version N+1 to the Tuya platform and launch the update from the app | "Actualizando..."; boots N+1; `image=valid` after the health check (≥ 30 s and MQTT/USB) | | |
| E2E-51 Rollback | Install by OTA an image that fails the health check (test image) | The bootloader goes back to version N in ≤ 10 min | | |
| E2E-52 Configuration after OTA | After E2E-50 | Credentials, agent key, `wake_method` and `cmd_countdown` are preserved | | |

## 7. Stability (24 h)

| Case | Steps | Expected | Result | Evidence |
|---|---|---|---|---|
| E2E-60 24 h powered on | PC on with the agent for 24 h; record `!status` (`heap` line) at the start, every ~6 h and at the end | No dongle restarts (continuous `uptime`, `reset=` without crash); stable free heap; **minimum heap recorded:** ____ | | |
| E2E-61 24 h with cycles | During the 24 h: ≥ 5 suspend/power-on cycles and ≥ 2 shutdown/power-on | All correct; no leaks (minimum heap with no downward trend) | | |
| E2E-62 Agent 24 h | `systemctl status microesp-agent`, `journalctl` and RSS (`ps -o rss`) | No service restarts or repeated errors; stable RSS | | |

## Summary

| Total | ✅ | ⚠️ | ❌ (bloqueantes) | N/A |
|---|---|---|---|---|
| | | | | |

Open issues (id, case and description):

-
