# Changelog

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning follows [SemVer](https://semver.org/). Firmware and agent share the version number (tag `vX.Y.Z`, which must match `CONFIG_PROJECT_VERSION` in `firmware/app_default.config`).

## Unreleased

### Fixed

- **Panel: Restart button out of view.** The Shut down / Restart row used `width: 100%` buttons, but the MiniApp button default style won and pushed Restart past the right edge of the card. Both now take equal halves (`flex: 1 1 0`, `min-width: 0`).

## 0.4.1 - 2026-10-05

### Fixed

- **Firmware: PC reported on while it is off after a remote shutdown.** On the Lenovo ThinkStation P3 Ultra the BIOS/EC re-enumerates the dongle in S5 on its powered port and keeps the bus active, so `pc_state` went `booting` → `on_no_agent` (with the `agent_lost` fault) for hours. An acked shutdown now holds `pc_state=off` until an operating system is back (agent session `ready` or CDC port opened by the host); a bus mount or resume no longer clears it. A power-on in progress still shows `booting` and falls back to `off` if it fails.

## 0.4.0 - 2026-10-05

### Added

- **User scripts.** Up to 5 scripts defined in `agent.toml` (`[[scripts]]`: `id`, `label`, `command`, optional `timeout`, default 10 min) can be started from the panel. The agent sends only ids and labels to the dongle (new CDC message `scripts`, cdc-v1 §3.1); the command line never leaves the PC. The dongle publishes them in DP 115 `scripts` and runs one when DP 116 `script_run` is set to its id, through a signed `cmd` with action `script:<id>`; the outcome goes to `last_result`. The panel shows one button per script, with confirmation, only while the agent is online and has scripts. Scripts run in the background, one instance per id, killed with their process group on timeout. The agent refuses scripts from a config file writable by group or others.
- **User scripts runner.** `install.sh --scripts-user NAME` installs `microesp-scripts.socket` and `microesp-scripts.service` (`microesp-agent scripts-runner`, socket-activated on `/run/microesp/scripts.sock`) running as that desktop user without a sandbox, and sets `scripts_socket` in `agent.toml`. The agent service is unchanged (user `microesp`, full sandbox, same polkit rule): it only sends the script id over the socket and acks after the runner answers. The runner takes commands only from the root-owned `agent.toml` and accepts only the `microesp` user or root (`SO_PEERCRED`). Without the runner, scripts run inside the agent as `microesp` in its sandbox (no network, no `/home`). On Windows scripts run in the service as LocalSystem.

### Changed

- **Firmware string DPs.** Strings live in one pool sized per DP (less RAM than before), reports go up to 1920 bytes, and each string DP of a report gets its own snapshot (several string DPs in one report were broken). Power and script commands each keep their own pending ack, so a script never disturbs a shutdown or reboot countdown.

## 0.3.0 - 2026-10-05

### Added

- **Panel: scheduling.** New *Schedule* section to power on, shut down or restart the PC "in X h Y min" (one-shot) or "at a time" on chosen weekdays (none = once, all = every day). They are Tuya cloud timers (`addTimer`, one category per command, always writing `true`), so the dongle needs no timer support and a scheduled shut down or restart still runs the cancelable countdown. Scheduled timers are listed soonest first and can be removed. Tuya's generic timer page (`openTimerPage`) does not open for this TuyaLink product, so the panel builds its own.

### Changed

- **Panel: power commands always available.** The BIOS enumerates the dongle even with the PC off, so the detected `pc_state` is only a hint: power on, shut down and restart are always shown and enabled. Shut down and restart still ask for confirmation and run the cancelable countdown; with the agent offline the panel only warns that the dongle will reject them.
- **Panel: no `hid_not_armed` banner.** It is informational (as for the LED); a failed wake is still reported as `wake_failed`.

## 0.2.0 - 2026-10-05

### Changed

- **Documentation and comments in English.** `docs/usuario/` is now `docs/user/` (`instalacion.md` → `installation.md`) and `docs/analisis/` is now `docs/analysis/` (`00-architecture-analysis.md`). Product UI text (dongle screen, agent CLI output, panel strings) is unchanged.
- **Power-on always sent, and Alt+P (Lenovo Smart Power On).** With *Smart Power On* the BIOS keeps the keyboard enumerated in S5, so the dongle believed the PC was on and ignored the command. Now the power-on command (app, button, `!wake force`) is always sent: with the bus active it taps Alt+P; when suspended, *remote wakeup* and Alt+P on resume; when not mounted, forced *resume*; with 2 retries and WOL according to `wake_method`. Success = agent online or the dongle re-enumerating after the Alt+P. A command during a power-on in progress resends the HID step. The default method `hid_then_wol` now sends WOL **at the same time** as HID (and repeats it after 20 s without success), instead of only when HID fails: the dongle cannot know whether the BIOS ignored the Alt+P. `mhal_hid_tap` waits for the host to read the report before releasing the key (avoids stuck keys with slow hosts). `!status` shows `hid_proto`.
- **Cloud: TuyaLink instead of TuyaOS** (ADR-5). TuyaOS licenses (UUID/AuthKey) cannot be obtained. The firmware stops using TuyaOpen's `tuya_iot` client and speaks TuyaLink (MQTT 3.1.1 over TLS, port 8883, server verification with the ESP-IDF CA bundle) with its own client on top of `esp-mqtt`:
  - HMAC-SHA256 signature of the username/password with the SNTP time on every attempt; reconnection with 2, 4, 8, 16 and 32 s waits and then every 120 s;
  - `property/report` with the thing-model codes (enums as strings, `fault` as an integer), one report in flight confirmed by PUBACK, same thresholds and throttling, everything reported on every connection;
  - `property/set` with several properties, validated one by one and answered with `property/set_response`; `action/execute` answers with an error (no actions); `model/get` on every connection.
  - TuyaOpen is kept as the framework (RTOS, LVGL, build). Image from 1.59 to 1.36 MB and ~90 KB more internal heap (no BLE or `tuya_iot`).
- **Provisioning** through the CDC CLI: `!tylink <region> <productId> <deviceId> <deviceSecret>` and `!wifi <ssid> <password...>` (the password is the rest of the line), stored in NVS, with the same release policy as before (only when unprovisioned or within the 120 s window of the 5 s button press; always in development, plus `!tylink clear` / `!wifi clear`).
- `!status` shows region, productId, masked deviceId, Wi-Fi/MQTT/SNTP state, last error, reports and commands received, without secrets.
- The `cloud_lost` fault is also raised if the cloud never manages to connect (60 s after boot).
- The log censor now covers ESP-IDF lines (Wi-Fi, MQTT, TLS) and registers the deviceSecret, the Wi-Fi password and the MQTT password of each attempt.

### Removed

- BLE/AP pairing with Smart Life, `!auth`, `!pid` and `!reset-tuya` (they answer "not used with TuyaLink"), `include/tuya_secrets.h(.example)`, the Tuya reset with the button (10-20 s no longer does anything) and the TuyaOpen CLI on UART0.
- OTA through the cloud (it was done by `tuya_iot`): pending with the TuyaLink OTA topics; update over USB.

### Added

- **Panel build and publishing guide** ([`docs/panel-publishing.md`](docs/panel-publishing.md)): the Tuya MiniApp IDE on Linux, phone preview, and the create panel → link in the IDE → upload → review → release → select on the product flow.
- **Tuya MiniApp IDE under Wine** (`panel/scripts/ide-wine/`): `setup.sh` unpacks the Windows IDE from its installer, installs Node for Windows in the prefix and applies `patch-ide.mjs`, which makes the IDE run the Ray CLI directly instead of through PowerShell (a stub under Wine); `run-ide.sh` launches it. `npm run ide:win-natives` adds the Windows native packages the IDE build needs.
- **Own panel for Smart Life** (`panel/`, Panel MiniApp with Ray): PC state, power on, shut down and reboot with confirmation and cancellable countdown, telemetry, `fault` warnings and settings (`wake_method`, `cmd_countdown`). It reads and writes through the TuyaLink thing model (`publishThingModelMessage`, `onReceivedThingModelMessage`), with texts in Spanish and English. The tests (`npm test`) check the model against `firmware/schema/dp.json`. Tested with the real device and released: it is the product panel in Smart Life.
- `src/core/tylink.c` (TuyaLink in plain C) and 14 new host tests (80 in total): HMAC vectors computed with Python, report JSON and responses, `property/set` with several properties, unknown codes, wrong types, out of range, malformed and truncated messages; `!wifi` parsing and obsolete commands.

### Fixed

- `schema/dp.json`: the report policy ids still followed the old numbering (106-108/111); now 105-107/110.

## 0.1.0

### Added

- **Firmware** (TuyaOpen v1.9.0 / ESP-IDF v5.4, 16 MB `POCKET_DONGLE_S3` board):
  - composite USB device HID keyboard + CDC ACM with *remote wakeup*;
  - power-on by HID with Wake-on-LAN fallback (`hid_then_wol`);
  - shutdown and reboot with cancellable countdown;
  - PC state machine (DP 101) and DPs 101–115 (except 105);
  - BLE/AP pairing with Smart Life;
  - OTA with rollback;
  - button (gestures), status LED and ST7735 screen with LVGL (basic version);
  - CLI over CDC and safety nets to reflash without BOOT.
- **cdc-v1 protocol** (`docs/protocol/cdc-v1.md`): session authenticated with HMAC-SHA256, pairing with a 6-digit code (HKDF) and normative vectors in `protocol/testdata/vectors.json`.
- **Agent** `microesp-agent` (Go):
  - telemetry (CPU, memory, free disk, uptime, hostname, MACs) and heartbeat;
  - execution of signed commands through systemd/logind with minimal polkit;
  - idempotent installer with udev, polkit and a sandboxed systemd unit;
  - optional Windows service.
- **Tests**: 59 Unity tests of the firmware core on the host (ASan/UBSan) and agent tests with `-race`.
- **CI** (GitHub Actions): agent vet/staticcheck/gofmt/tests, firmware host tests, firmware build, gitleaks and shellcheck. The tag release uses goreleaser and publishes a common `SHA256SUMS`.
- **Documentation**: root README, installation guide, BIOS/OS guide for the Lenovo ThinkStation P3 Ultra SFF G2 and E2E test plan.

### Pending

- Validate power-on from S4/S5 on the Lenovo (MESP-US-0002) and run the E2E plan (`docs/qa/e2e-v1.md`).
- Real Tuya PID and credentials; real OTA from the Tuya platform.
- Interface polish (phase B) and VBUS measurement (DP 105).
