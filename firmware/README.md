# MicroESP firmware (phase A)

Production firmware for the **MicroESP** dongle (Pocket-Dongle-S3, a clone of the LilyGO T-Dongle-S3) on **TuyaOpen v1.9.0 / ESP-IDF v5.4**, board `POCKET_DONGLE_S3`. It reuses what was validated in the `hw/spikes/tuya-usb/` spike (TinyUSB inside TuyaOpen, 16 MB board, reflashing without BOOT).

**Cloud: TuyaLink** (since 0.2.0, ADR-5 in `docs/analysis/00-architecture-analysis.md`). TuyaOS licenses (UUID/AuthKey) cannot be obtained, so the firmware no longer uses TuyaOpen's `tuya_iot` client: it speaks the open TuyaLink protocol (MQTT over TLS) with its own client on top of `esp-mqtt`. TuyaOpen is kept only as a framework (RTOS/`tal_*`, LVGL, build, partition table).

Status: phase A complete (everything except UI polish). Connected to the Tuya EU cloud with TuyaLink and verified on the board on 2026-10-04. LCD wiring, offsets and orientation verified by reading the factory firmware over USB-JTAG (`hw/pinout.md`). Pending: LED type and pin.

Stories: MESP-US-0009, 0010, 0011, 0012, 0013, 0014, 0015, 0016, 0017/0018/0020 (basic version), 0022 and 0023.

## Structure

```
firmware/
├── app_default.config     # board, LVGL and VERSION (CONFIG_PROJECT_VERSION, single source)
├── sdkconfig.microesp     # ESP-IDF overlay: TinyUSB and OTA rollback
├── Kconfig                # MESP_DEV_CLI option (development CLI; n = release)
├── build.sh               # builds (tos.py): release by default, "dev", "clean" and "test"
├── install-board.sh       # registers board/POCKET_DONGLE_S3 in the TuyaOpen checkout
├── board/POCKET_DONGLE_S3 # TuyaOpen board (16 MB, PSRAM)
├── include/
│   ├── mesp_board.h       # PINS and LCD/LED/button parameters (a single place)
│   └── mesp_hal.h         # API between the TuyaOpen app and the ESP-IDF component
├── src/                   # TuyaOpen app (does not see ESP-IDF headers)
│   ├── app_main.c         # boot, event bus and application task
│   ├── cloud.c            # TuyaLink client: provisioning, reports and commands
│   ├── usb_composite.c    # USB events → app and hid_not_armed
│   ├── agent_link.c       # cdc-v1 session with the agent
│   ├── pairing.c          # agent pairing mode (6-digit code)
│   ├── state.c            # pc_state (DP 101) and fault bitmap (DP 114)
│   ├── wake.c             # HID / WOL power-on
│   ├── power.c            # shutdown/reboot with countdown
│   ├── scripts.c          # user scripts: list (DP 115) and runs (DP 116)
│   ├── button.c led.c display.c ota.c cli.c
│   └── core/              # pure C logic, NO RTOS/IDF dependencies (host tests)
│       ├── link_proto.c   # cdc-v1 protocol (parser, session, signing, pairing)
│       ├── mesp_crypto.c  # HMAC-SHA256 and HKDF-SHA256 (mbedTLS)
│       ├── pc_state.c     # PC state machine
│       ├── powercmd.c     # countdown → cmd → ack flow
│       ├── scriptcmd.c    # script run → cmd script:<id> → ack flow
│       ├── wake_fsm.c     # power-on sequence and WOL magic packet
│       ├── button_fsm.c   # button gestures
│       ├── dp_model.c     # DP table, thresholds and throttling
│       ├── tylink.c       # TuyaLink: credential signing, topics, report/command JSON
│       ├── cli_policy.c   # which CLI commands each build accepts, and the provisioning window
│       └── log_redact.c   # redaction of log lines containing secrets
├── esp_components/mesp_hal/  # ESP-IDF component: TinyUSB, NVS, OTA, LCD, LED, button, WOL,
│                             #   Wi-Fi + SNTP + MQTT/TLS (hal_cloud.c)
├── schema/dp.json         # DP description (kept in sync with dp_model.c, checked by a test)
├── test/host/             # Unity tests on the PC (gcc + Makefile, ASan/UBSan)
└── tools/                 # flash.sh, touch1200.py, mesp_cdc.py, set_build_mode.py
```

## Architecture

```
 Tuya cloud ⇄ TLS ⇄ [HAL: mesp_cloud (Wi-Fi, SNTP, MQTT loop) + esp-mqtt] ─┐ events
 PC (agent) ⇄ CDC ⇄ [HAL: TinyUSB, line dispatcher, supervisor] ─┤ (queue)
 Button GPIO0 ─────────────────────────── polled every 20 ms ─┐        │
                                                            ▼        ▼
                         [mesp_app task]: owner of ALL application state
                          link · power · wake · state · DPs · LED · CLI · OTA
                                         │ snapshot (mutex)
                                         ▼
                         [mesp_ui thread]: LVGL v9 → ST7735 (SPI + DMA)
```

- **Event bus**: TuyaOpen queue (`tal_queue`) of `app_ev_t` (`app_post()`, never blocks). The TinyUSB, MQTT client, Wi-Fi and CDC dispatcher callbacks only post events. The `mesp_app` task consumes them and runs a 20 ms tick (button) and a 100 ms tick (timeouts, state, DP reporting, LED and display). This way all the logic runs in a single thread and needs no locks.
- **Cloud (TuyaLink)**: the HAL task `mesp_cloud` owns the connection: it retries Wi-Fi, waits for SNTP time, asks the app for the username/password of each attempt (the signature includes the time), creates the `esp-mqtt` client (TLS with the ESP-IDF CA bundle), subscribes and reconnects with a back-off of 2, 4, 8, 16 and 32 s and then every 120 s (the counter resets to 0 after a connection lasting ≥ 60 s). All the logic (what to report, received commands, replies) runs in the `mesp_app` task: `mhal_cloud_publish()` only puts the message in the `esp-mqtt` *outbox* under a mutex, with no network I/O in the caller. At most one `property/report` is in flight: it ends with its PUBACK (`EV_CLOUD`), and fails on disconnect or after 30 s. Received messages arrive as `EV_CLOUD_RX` (a copy of the payload) and are processed in the app task.
- **Software watchdog**: the `mesp_app` task notifies the HAL supervisor on every loop. If more than 60 s pass without it doing so, the supervisor resets the chip.
- **App/HAL split**: the code in `src/` is built by TuyaOpen's CMake, which does not see the ESP-IDF headers. Everything IDF-specific lives in `esp_components/mesp_hal` (a component linked with `WHOLE_ARCHIVE`), and the app uses it through `include/mesp_hal.h`, which contains only standard C types.
- **Testable core**: `src/core/` is pure C (cJSON + mbedTLS) and is tested on the PC.
- **Version**: `CONFIG_PROJECT_VERSION` in `app_default.config` → `PROJECT_VERSION` (TuyaOpen) → `MESP_FW_VERSION` (`src/mesp_version.h`). It is used in `welcome.fw`, on the display, in `!version` and in the boot log.
- **Partitions**: TuyaOpen's `partitions_16M.csv` with dual OTA: nvs, otadata, `ota_0` and `ota_1` of 7.4 MB each, `model`, `tuya` KV and `factory_nvs`.
- **Why TuyaOpen is kept**: it is the lowest-risk option. All the app code uses `tal_*` (queues, threads, log, KV), the display uses its LVGL, and the build, the 16 MB board and the USB safety nets are validated on it. Removing `tuya_iot` changes none of that: the linker no longer includes the Tuya client or BLE (smaller image and ~90 KB more internal heap). Two side effects were resolved: the mbedTLS mutex *hooks* (`MBEDTLS_THREADING_ALT` in TuyaOpen's sdkconfig) used to be installed by `tuya_tls_init()` and are now installed by `hal_cloud.c`; and TuyaOpen's CLI on UART0 (`auth`) is no longer started.

## Build

Environment: `docs/dev-setup.md` (TuyaOpen at `/www/MicroESP/tools/TuyaOpen`).

```bash
cd firmware
./build.sh            # incremental, RELEASE (MESP_DEV_CLI=n: restricted CLI, NOTICE log)
./build.sh dev        # incremental, DEVELOPMENT (MESP_DEV_CLI=y: full CLI, DEBUG log)
./build.sh clean [dev] # after changing sdkconfig.microesp, the board or app_default.config
./build.sh test       # host tests only (no toolchain needed)
# output: dist/microesp_<ver>/{bootloader.bin, partition-table.bin, ota_data_initial.bin, microesp.bin, srmodels.bin, microesp_QIO_<ver>.bin}
```

Both variants write to the same `dist/`: whichever was built last wins. `tools/set_build_mode.py` regenerates `.build/cache/using.config` (and deletes `using.cmake` and `tuya_kconfig.h`) only when the variant changes, and `build.sh` checks at the end that the generated header matches the requested one. On the board the variant is visible in `!help`, in `!status` (`cli: build=...`) and in the boot line of the log (`cli=dev|release`).

Memory (`python -m esp_idf_size dist/microesp_0.1.0/microesp_0.1.0.map` in the IDF environment; do not use `idf.py size`, which reconfigures the project):

| | v0.1.0 (TuyaOS) | v0.2.0 (TuyaLink) |
|---|---|---|
| App image (release) | 1,594,224 B (21% of a 7.4 MB slot) | **1,364,192 B** (18%); dev: +~450 B |
| Flash `.text` / `.rodata` | 1,147 KB / 304 KB | 920 KB / 323 KB |
| Static IRAM | 16,383 / 16,384 B | 16,383 / 16,384 B (unchanged; nothing new in IRAM) |
| Static D/IRAM | 144 KB used / 198 KB free | 120 KB used / 216 KB free |
| Internal heap at runtime | ~64.5 KB free (min 62.6 KB) with Wi-Fi, BLE, TinyUSB and LVGL | **~154.8 KB free** (min ~153.9 KB) with Wi-Fi, TLS/MQTT connected, TinyUSB and LVGL; PSRAM free 8.18 MB (mbedTLS uses PSRAM) |

## Flash (without pressing BOOT)

```bash
sg dialout -c "tools/flash.sh"        # full (bootloader, table, otadata, app, srmodels)
sg dialout -c "tools/flash.sh --app"  # otadata + app only
```

`flash.sh` does a *1200-baud touch* on the CDC: the firmware switches to ROM download mode (`303a:1001`). It then runs esptool with the **default** reset sequence. Never use `--before no_reset`: the final reset via RTS would leave the board in download mode again.

| Way into download mode / USJ | Effect |
|---|---|
| 1200-baud touch (`tools/touch1200.py`) or `!dfu` | ROM download mode |
| `!usj` | reboots once without TinyUSB (USB-Serial/JTAG, normal esptool) |
| BOOT held ≥ 20 s | download mode (handled by the HAL supervisor, works even if the app is hung) |
| Last resort | BOOT pressed while plugging in |

Verified on the board: 1200-baud touch, `!dfu` and `!usj`, each followed by a normal esptool.

**Safety nets** (inherited from the spike and extended):
- If it does not enumerate within 20 s, it boots once without TinyUSB (USJ). If in that boot there is no USB host (no SOF for 60 s, for example with the PC off and "Always On USB"), it goes back to TinyUSB with this fallback inhibited. This keeps HID available to power the PC on.
- After 3 consecutive crash reboots, it boots without TinyUSB.
- `!log` dumps the 64 KB log buffer in PSRAM (the TuyaOpen log goes to UART0, which is not accessible).

## TuyaLink provisioning and Wi-Fi

The device is registered on the Tuya platform (a product with the DP model from `schema/dp.json`, TuyaLink connection), which provides **productId**, **deviceId** and **deviceSecret**. It is bound to the app account from the platform itself (already done for the development device). There is no BLE/AP pairing: it has been removed.

Everything is stored in NVS (namespace `microesp`: `tl_region`, `tl_pid`, `tl_did`, `tl_dsec`, `wifi_ssid`, `wifi_pass`) through the CDC CLI and applied with `!reboot`. There are no build-time credentials (`tuya_secrets.h` is no longer used).

```bash
!tylink <eu|us|cn|in> <productId> <deviceId> <deviceSecret>
!wifi <ssid> <password>        # the password is the REST of the line: it may contain spaces
!reboot
```

- Region → broker: `eu` m1.tuyaeu.com, `us` m1.tuyaus.com, `cn` m1.tuyacn.com, `in` m1.tuyain.com (port 8883, TLS with verification of the server certificate against the ESP-IDF CA bundle; `*.tuyaeu.com` is signed by GoDaddy, root "Go Daddy Root Certificate Authority - G2").
- Validation: alphanumeric ids of 8 to 32 characters; secret of 8 to 64 printable characters without spaces; SSID without spaces, 1 to 32 bytes; Wi-Fi password mandatory, 8 to 64 characters (open networks are not supported, so that a forgotten password does not store an open network). 2.4 GHz Wi-Fi only.
- Connection: `clientId=tuyalink_<deviceId>`, `username=<deviceId>|signMethod=hmacSha256,timestamp=<s>,secureMode=1,accessType=1`, `password=hex(HMAC-SHA256(deviceSecret, "deviceId=<id>,timestamp=<s>,secureMode=1,accessType=1"))`, *keepalive* 60 s. The time comes from SNTP (`pool.ntp.org`, `time.google.com`); no connection is attempted until it is synchronized on each boot. There can only be **one connection per deviceId**: another client with the same id (for example `hw/spikes/tylink_test.py`) kicks the dongle out.

**Provisioning in release**: `!tylink` and `!wifi` are only accepted if that datum is not yet in NVS (the first time) or during the **provisioning window**: 120 s after holding the button **between 5 and 10 s** and releasing it (the display shows "Suelta: aprovisionar" and then "Aprovisionar 120 s"; `!status` reports `provisioning_window=<s>`). Outside the window they reply `err: ... is locked`. In development they are always accepted, and `!tylink clear` / `!wifi clear` erase that data from NVS.

Secrets are never printed: `!status` shows the region, the productId, the masked deviceId (`26e0...0z`), the SSID and the state, never the deviceSecret or the password. The deviceSecret, the Wi-Fi password and each derived MQTT password are registered in the log redactor (see "Logs"). The CLI logs only the command name.

The TuyaOS-era commands (`!auth`, `!pid`, `!reset-tuya`) reply `err: ... is not used with TuyaLink (use !tylink and !wifi)`.

When not provisioned, the LED blinks blue and the display shows "Nube: !wifi/!tylink".

## Agent pairing (cdc-v1 §4)

The dongle enters pairing mode if it has no key at boot or when the button is held between 3 and 5 s (physical presence). In development also with `!pair`. The random 6-digit code is shown **only on the display** for 120 s: it is never logged nor sent over the CDC, and it is also registered in the log redactor. After 3 wrong codes it leaves the mode. `!unpair` (development only) erases the key; in release, a new button pairing replaces the previous key.

```bash
microesp-agent pair --config agent.toml [--code 123456]
```

## CLI over the CDC

Lines starting with `!` are CLI and lines starting with `{` are protocol. The port is the same one the agent uses, and the agent opens it exclusively: to use the CLI the agent must be stopped first. The dongle closes the agent session as soon as the host closes the port (DTR drops).

Any process with access to the port (`dialout` group or root) can write to the CLI. That is why there are two build variants (`Kconfig`: `MESP_DEV_CLI`; policy in `src/core/cli_policy.c`, with tests):

| Command | Release | Dev | What it does |
|---|---|---|---|
| `!status` | yes | yes | summary: version, USB, pc_state, faults, agent, telemetry, power, scripts (ids of the agent's list, runs and results), wake, pairing, variant/window, TuyaLink (region, productId, masked deviceId, MQTT state, attempts, last error, reports and how long ago the last one was, received commands), Wi-Fi (SSID, IP, RSSI, SNTP time), heap. No secrets |
| `!version`, `!dp`, `!help` | yes | yes | version / current DP values (JSON) / help |
| `!log` | yes | yes | log buffer (HAL), redacted (see "Logs") |
| `!cancel` | yes | yes | cancels the countdown or the scheduled `!cmd` |
| `!reboot`, `!dfu`, `!usj` | yes | yes | reboot / download mode / one boot without TinyUSB (HAL) |
| `!tylink <region> <productId> <deviceId> <deviceSecret>`, `!wifi <ssid> <password...>` | not provisioned or within the 120 s window | yes | TuyaLink / Wi-Fi → NVS (applied with `!reboot`) |
| `!tylink clear`, `!wifi clear` | no | yes | erase that data from NVS |
| `!auth`, `!pid`, `!reset-tuya` | no | no | obsolete with TuyaLink (clear error) |
| `!pair`, `!unpair` | no | yes | agent pairing mode / forget the key |
| `!wake [force]` | no | yes | without `force` it only shows the plan (dry run); with `force` it runs the power-on. Ignored if the PC is on |
| `!method <hid\|wol\|hid_then_wol>`, `!countdown <0..60>` | no | yes | DP 109 / DP 112 without going through the cloud |
| `!cmd <shutdown\|reboot> [countdown] [delay_s]` | no | yes | simulates DP 103/104. The delay allows starting the agent before it fires |
| `!key` | no | yes | presses and releases left Shift (harmless; only with the bus active) |

In release, a development command replies `err: <cmd> needs a development build` and a blocked provisioning command replies `err: <cmd> is locked`. Each command is logged only by its name (never the arguments), marked `(refused)` if rejected. `!dfu` and the *1200-baud touch* remain available in release so the board can be reflashed without the button. This means that anyone with access to the port can install other firmware; it is the same trust boundary as root on the PC.

### Logs

The TuyaOpen and app log goes out through UART0 and is copied to the buffer that `!log` dumps; so are the ESP-IDF logs (Wi-Fi, `esp-mqtt`, TLS). In release the app level is NOTICE and in development DEBUG (ESP-IDF stays at INFO). In both variants, `src/core/log_redact.c` replaces with `[redacted: sensitive log line]` any line containing a registered secret (TuyaLink deviceSecret, Wi-Fi password, MQTT password of the current attempt, pairing code) or a sensitive word (`authkey`, `localkey`, `seckey`, `secret`, `regist_key`, `token`, `passwd`, `password`, `psk`...). The filter is also applied to ESP-IDF lines (`mhal_log_set_filter`), in the buffer and on UART0; for those lines only the first 255 bytes are examined. The agent key is binary and is never printed.

Tool: `tools/mesp_cdc.py '!status' '!dp'` (with `sg dialout` and the IDF environment's Python, which ships pyserial).

## Button (GPIO0)

| Gesture | Action |
|---|---|
| Short press | cancels the shutdown/reboot countdown; if there is none, switches the screen |
| Double press (< 400 ms) | powers the PC on (DP 109 method) |
| Hold 3-5 s and release | agent pairing mode |
| Hold 5-10 s and release | provisioning window: `!tylink`/`!wifi` accepted for 120 s in release |
| Hold 10-20 s and release | no action since TuyaLink (there is no BLE/AP binding to reset) |
| Hold ≥ 20 s | ROM download mode (recovery) |

40 ms debounce. Presses of 1 to 3 s are ignored. No gesture is accepted until the button has been seen released at least once (protects against a GPIO0 stuck low at boot). While held, the display shows what will happen on release. Change from the spike: there, ≥ 2 s triggered download mode.

## LED

Driver selectable in `include/mesp_board.h`: `MESP_LED_TYPE` (`WS2812` via RMT or `APA102` via bit-bang), `MESP_LED_PIN` (40 by default), `MESP_LED_PIN_CLK` and `MESP_LED_MAX_BRIGHTNESS` (48/255, low brightness).

| Priority | Situation | Color |
|---|---|---|
| 1 | shutdown/reboot countdown | red, fast blink (4 Hz) |
| 2 | power-on sent (waiting for the PC) | white, pulsing |
| 3 | pairing (agent) or cloud not provisioned (`!tylink`/`!wifi`) | blue, blinking (1 Hz) |
| 4 | error (faults except `hid_not_armed`; includes `cloud_lost`) | solid red |
| 5 | PC on with agent | green |
| 6 | PC on without agent / booting | amber (booting: blinking) |
| 7 | PC off / suspended / unknown | dim white |

## Display (basic version)

ST7735 in landscape (160×80) with TuyaOpen's LVGL v9 in its own thread. Pins, offsets (`x=1`, `y=26`), `MADCTL=0x68`, inversion and backlight polarity are in `include/mesp_board.h` and can be overridden with `-D`.

Screens: **status** (PC state, hostname, Wi-Fi/cloud/agent icons and version), **telemetry** (CPU / MEM / free disk bars), **countdown** (automatic, with "Pulsa para cancelar") and **pairing code** (automatic). A short press toggles between status and telemetry; with the agent connected they rotate on their own every 5 s. A bottom line shows temporary notices. Polish (icons, typefaces, `docs/ui.md`) is left for phase B.

## DPs

Full table in `schema/dp.json`. In TuyaLink each DP is a **property** of the thing model identified by its **code** (`pc_state`, `power_on`...); the numbers 101-116 are the platform's `abilityId` and are kept as the internal key and in the documentation. JSON encoding: bool → `true/false`; value → integer (scale 1: tenths of %); **enum → string** (`"on"`, `"hid_then_wol"`...; verified against the model returned by `model/get_response`); bitmap (`fault`) → integer with the mask; string → string (escaped: DP 115 carries compact JSON inside a JSON string). Topics (`tylink/<deviceId>/thing/...`): it publishes `property/report`, `property/set_response`, `action/execute_response` and `model/get` (once per connection); it subscribes to `property/set`, `action/execute`, `model/get_response` and `property/report_response`.

- **Commands (`property/set`)**: it may carry several properties. Each one is validated (known code, writable, correct JSON type, range; for the only writable string, `script_run`: `""` or `[a-z0-9_-]{1,12}`); the valid ones are applied in order and the others are rejected one by one. The reply is `property/set_response` with the same `msgId` and `code` 0 if all were valid or 1 if any was rejected. A message without `msgId` (1..32 characters) or without a `data` object is discarded without a reply. `action/execute` always replies `code` 1 (the model has no actions).

Summary:

- **101 `pc_state`**: PC state. **102 `power_on`**: push button; returns to `false`. **103/104 `power_off`/`reboot`**: `true` starts the countdown and `false` during the countdown cancels it; they return to `false` when it ends.
- **105/106/107**: CPU, memory and free disk in tenths of %, from 0 to 1000. **108 `agent_online`**. **109 `wake_method`**: stored in NVS. **110 `pc_uptime`**. **111 `pc_hostname`**. **112 `cmd_countdown`**: from 0 to 60, 10 by default, stored in NVS. **113 `last_result`**.
- **114 `fault`**: bit0 `agent_lost`, bit1 `wake_failed`, bit2 `hid_not_armed`, bit3 `cloud_lost` (no `vbus_low`). Consecutive ids: the Tuya platform assigns them in sequence.
- **115 `scripts`** (string, ro, max 255): the agent's user scripts as compact JSON `[["<id>","<label>"],...]` (0..5 items, at most 221 bytes). Not reported until the agent has sent its list once. **116 `script_run`** (string, rw, max 12): push button; see "User scripts".
- **Before flashing a firmware with DPs 115/116**, create them in the Tuya platform (product → function definition → custom TuyaLink properties): code `scripts`, type string, max length 255, read-only (report only); code `script_run`, type string, max length 12, read-write (send and report). The platform assigns the `abilityId`s in sequence: check that they are 115 and 116 (the firmware only uses the codes on the wire, but the docs, `schema/dp.json` and the panel use the numbers). Without them the cloud may reject every report that carries them, including the other properties of the same message (`property/report_response` with an error code, counted in `!status` as `cloud_report_errors`).
- **Reporting policy**: asynchronous, from the application task and only with MQTT connected.
  - Telemetry: if it changes ≥ 20 tenths (with at least 5 s between reports) or any change every 30 s.
  - Uptime: at most one per minute.
  - The rest: on change.
  - Each DP has a minimum interval ≥ 300 ms (≤ 200 reports/DP/min).
  - On each MQTT connection all of them are reported.
  - A report is considered good with its PUBACK (QoS 1). If it fails (disconnect, no PUBACK within 30 s or no connection when queuing it), it is retried after 5 s.

## PC state (US-0015)

Four signals are merged: the USB bus state (mounted/suspended), whether the agent is online (heartbeat with a 15 s timeout, or port closed), whether a power-on is in progress, and whether the agent has confirmed a shutdown (`ack`). The first rule that matches is applied, with hysteresis:

| Condition | State | Hold |
|---|---|---|
| agent online | `on` | 0 s |
| power-on in progress and bus not active | `booting` | 0 s |
| shutdown confirmed, power-on in progress | `booting` | 0 s |
| shutdown confirmed | `off` (S5 with a powered port looks like a suspend or, on the Lenovo, like an active bus re-enumerated by the BIOS/EC) | 3 s |
| mounted + suspended | `sleep` | 3 s |
| not mounted | `off` | 3 s |
| bus active and the agent was online since the last rising edge | `on_no_agent` (plus `agent_lost` fault) | 2 s |
| bus active and < 90 s since the rising edge (mount, resume or power-on) | `booting` | 0 s |
| bus active | `on_no_agent` | 2 s |

It starts at `unknown` and sets the first state after 3 s.

`shutdown_expected` (shutdown confirmed with `ack`) is cleared only when an operating system is proven back: an agent session gets `ready`, or the host opens the CDC port (a BIOS/EC host never does). A bus mount or resume does not clear it: in S5 the Lenovo re-enumerates the dongle on its powered port and keeps the bus active for hours, which used to show `booting` → `on_no_agent` with the PC off ([`docs/analysis/spike-wake.md`](../docs/analysis/spike-wake.md)). While it is set no `agent_lost` fault is raised. Side effect: after a remote shutdown, a boot with the power button shows `off` until the agent opens the port.

`hid_not_armed` is evaluated on each suspend: it is set if the host suspends the bus without arming remote wakeup. It is the last known value.

## Power-on (US-0014)

The command is **never ignored**, even if the PC seems to be on: with Lenovo *Smart Power On* the BIOS keeps the keyboard enumerated in S5. A command during a power-on in progress resends the HID step.

- **HID** (3 attempts 2 s apart):
  - Bus active (mounted and not suspended): **Alt+P** (Smart Power On).
  - Bus suspended and wakeup armed: `tud_remote_wakeup()` and Alt+P when it resumes.
  - Not mounted or not armed (S4/S5): forced resume signalling (K state via the DWC2 `dcd_remote_wakeup`), 3 attempts 2 s apart. It only works if the BIOS watches the port in S4/S5 ("Wake on USB" / "Always On USB"); still to be validated on the Lenovo (MESP-US-0002).
- **WOL**: magic packet by UDP broadcast to ports 9 and 7, to `255.255.255.255` and to the subnet broadcast address, for each MAC received in the `hello` (at most 4, stored in NVS only after authenticating the session).
- **`hid_then_wol`** (default; the name is kept for compatibility with the DP): HID **and** WOL at the same time, and another WOL after 20 s if there is no success. With no known MACs, HID only.
- **Result**: `last_result=wake_sent` on sending. Success = the agent connects, or the PC enumerates the dongle again (new mount/resume) after the last Alt+P. No success within 120 s: `wake_failed` and the `wake_failed` bit, which is cleared by the next successful power-on.

## Shutdown / reboot (US-0016)

1. DP 103/104 set to `true`: if the agent is not online, `last_result=agent_offline`.
2. If it is online, the DP 112 countdown starts and `notice{action,in}` is sent. During the countdown it can be cancelled with the short press, with the DP set to `false` or with `!cancel`; the result is `cancelled` and `notice{cancel}` is sent.
3. When the countdown ends the signed `cmd` is sent (HMAC with the session nonces and an id that increases per session).
4. With `ack ok`, `last_result=ok`. With a negative `ack` or no reply within 10 s, `cmd_rejected`.
5. DP 103/104 return to `false`.

If the agent session drops with a pending `cmd` (port closed, agent restarted, new `hello` or re-pairing), the `ack` can no longer arrive. It is resolved at that moment as `cmd_rejected`. Previously the flow would wait forever and reject any later command as "busy".

## User scripts (cdc-v1 §3.1)

The agent defines up to 5 scripts (`[[scripts]]` in `agent.toml`); only their `id` and `label` reach the dongle, in a `scripts{list}` message after every `ready`. The dongle validates it (ids `^[a-z0-9_-]{1,12}$` and unique, labels 1..24 bytes of valid UTF-8 without control characters, `"` or `\`); an invalid list gets `err{bad_msg}` and the previous one is kept, and `scripts` before `ready` gets `err{unauth}`. The list lives in RAM only and is kept when the agent session drops (the panel hides it while `agent_online` is false); each valid list replaces it and updates DP 115.

1. DP 116 `script_run` set to `<id>` (`""` is a no-op). The DP is always reported back to `""`.
2. No authenticated agent session (or the agent is not online): `last_result=agent_offline`.
3. `<id>` not in the current list, or another script `cmd` still awaiting its `ack`: `cmd_rejected`.
4. Otherwise the signed `cmd{action:"script:<id>"}` is sent (same signature and id counter as shutdown/reboot). `ack ok` → `last_result=ok` (the agent runs the script in the background); negative `ack`, no `ack` within 10 s or the session dropping → `cmd_rejected`.

The link keeps one pending `cmd` per class (power / script), so a script run never delays, blocks or cancels a running shutdown/reboot countdown, and a shutdown `cmd` can be sent while a script `cmd` awaits its `ack`.

RAM: string DPs no longer reserve 2 × 65 bytes in every DP slot; `dp_model_t` has one pool with the current and the last reported copy of each string DP at its own maximum (64 + 255 + 12 bytes plus NULs, twice), which is less than before for 16 DPs. The report buffer is `TYL_REPORT_MAX` (1920 bytes, within the 2048-byte esp-mqtt buffer); a host test checks that every DP at its widest value fits.

## OTA and rollback (US-0011)

**Cloud OTA: not available since 0.2.0.** It was handled by TuyaOpen's `tuya_iot` client (`TUYA_EVENT_UPGRADE_NOTIFY`); the TuyaLink OTA topics are not implemented yet. Updates are done over USB (`tools/flash.sh`). The dual OTA table and rollback are kept:

**Rollback enabled**: the platform's sdkconfig had it disabled, and `sdkconfig.microesp` enables it with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`; the value was checked in the generated `sdkconfig`. A new image boots as `PENDING_VERIFY` and is marked valid after the health check: ≥ 30 s running and MQTT connected if the cloud is provisioned (an image that arrived through the cloud has to prove it can connect again). When not provisioned, MQTT connected or USB mounted is enough. If it does not pass within 10 min, the firmware reboots and the bootloader returns to the previous image; a crash before marking it also reverts it.

OTA over TuyaLink is still pending.

## Tests

```bash
./build.sh test        # = make -C test/host
```

80 Unity tests, with ASan and UBSan, using the Unity, cJSON and mbedTLS from the TuyaOpen/IDF checkout itself:

- **Cryptography and protocol**: all the normative vectors in `protocol/testdata/vectors.json` (HKDF key, signatures of `pair`/`pair_ok`/`welcome`/`auth`/`cmd`, valid and invalid messages and the wrong `cmd` signature).
- **Session**: handshake, `unauth`, `not_paired`, agent-online and `ack` timeouts, pending `cmd` resolved when the session drops, pairing with 3 failures and with the 120 s window, re-pairing and port close.
- **TuyaLink** (`test_tylink.c`): HMAC password with vectors computed in Python, username and clientId, hosts per region, id/secret validation, topics and their classification, `msgId`; `property/report` JSON (enums as strings, bitmap as integer, escapes, all DPs fit in the buffer), replies and `model/get`; `property/set` with one and several properties, unknown codes, read-only, wrong types, out of range, more properties than DPs, malformed and truncated envelopes; enum names equal to those of the core and of `schema/dp.json`.
- **Security**: release/dev CLI policy (which commands each variant accepts, `!tylink`/`!wifi` per datum and the 120 s physical window, obsolete commands, also when the ms counter wraps around), `!wifi` parsing (password with spaces = rest of the line) and log redaction (registered secrets, including the Wi-Fi and MQTT passwords, and sensitive words).
- **Robustness (fuzz)**: all truncations of each valid message, huge lines and lines of exactly 511/512 bytes, deep nesting, 37 malformed cases and 20,000 random lines.
- **Rest of the core**: PC state transition table, shutdown/reboot flow, power-on sequence and WOL, button gestures (including the 5 s one), and the DP model (including that it matches `schema/dp.json` and that a value that changes while a report is in flight stays pending).

### End-to-end test with the Go agent (always `dry_run`)

The pairing code no longer goes out over the CDC: it has to be read on the dongle's display (or reuse an already paired key that remains in NVS; flashing with `flash.sh` does not erase NVS). `!cmd` only exists in the development build.

```bash
go build -o $S/mea ./agent/cmd/microesp-agent
# agent.toml: device=/dev/serial/by-id/usb-MicroESP_*-if01, key_file=$S/agent.key, dry_run=true
./build.sh dev && sg dialout -c "tools/flash.sh --app"
tools/mesp_cdc.py '!pair'                                                # or button 3-5 s; the code, on the display
mea pair --config agent.toml --code NNNNNN
tools/mesp_cdc.py '!cmd reboot 3 25'                                     # fires 25 s later
timeout 60 mea run --config agent.toml --dry-run --log-level debug       # notice → cmd → ack → "DRY-RUN"
tools/mesp_cdc.py '!status' '!dp'                                        # last_result=ok, 114=0, 104=false
```

Result of 2026-10-03:
- Paired on the first attempt.
- `session ready` with fw 0.1.0, host `lenovop3` and 3 MACs.
- Telemetry received (DPs 106, 107, 108, 111 and 112) and `agent_online=true`.
- When `notice reboot in 3` fired, the signed `cmd id 1` arrived, the agent verified it and replied `ack ok`, and logged `DRY-RUN: power action not executed action=reboot`. On the dongle it ended with `last_result=ok`.
- With the agent stopped, `!cmd shutdown 0 0` gave `last_result=agent_offline`.

Repeated after the hardening (2026-10-03, same agent key stored in NVS):
- **Dev**: `!cmd reboot 3 25` → `notice reboot in 3` → `cmd id 1` → `ack ok` → `DRY-RUN: power action not executed action=reboot`; `last_result=ok`. With `!pair` the log only says "code on the display"; several TuyaOpen DEBUG lines (psk, regist_key...) appear as `[redacted: sensitive log line]`.
- **Release** (what remains on the board): enumerates as `303a:4002`. `!status` shows `cli: build=release` and contains no secrets. `!pair`, `!cmd`, `!wake`, `!key`, `!unpair` and `!reset-tuya` are rejected. `!auth` is accepted when not provisioned and then becomes locked; `!pid` likewise (the test data was erased with `!auth clear` in dev). The log has no DEBUG/INFO lines. With the agent: `session ready` (host `lenovop3`, 3 MACs) and 4 telemetry messages received (`tele: count=4`).

### End-to-end test with TuyaLink (2026-10-04)

Dongle provisioned with a development build (`!tylink` and `!wifi` sent by a script that reads the secrets without displaying them), EU region:
- Wi-Fi (2.4 GHz, RSSI -58) and IP in ~5 s; SNTP; **MQTT connected on the first attempt** with TLS verified against `m1.tuyaeu.com`; subscriptions granted; `model/get_response` received (1837 B, the thing model with the 14 codes); the first `property/report` (8 DPs) confirmed with PUBACK.
- Go agent in `dry_run` for 80 s: `session ready` (fw 0.2.0, host `lenovop3`, 3 MACs), 9 telemetry messages → 10 `property/report` confirmed (24 DPs reported in total), 0 failures, 0 disconnections.
- `!log` contains neither the deviceSecret, the Wi-Fi password nor the full deviceId (checked programmatically).
- Release (what remains on the board): connects on its own after flashing (NVS preserved); `!tylink`/`!wifi` locked (provisioned), `!tylink clear` locked, `!auth`/`!pid`/`!reset-tuya` obsolete, `!pair` development only; no DEBUG/INFO lines from the app.
- Not tested on the board: a real `property/set` command from the app after the change (the decoding has tests; with the Python spike `{"power_on":true}` was indeed received from the app) and the kick-out by a second client with the same deviceId (reconnection with back-off).

## Pending / known limitations

- **Visual confirmation by the user** (`hw/pinout.md`): display (wiring, offsets, colors), backlight and LED type/pin. All of it is changed in `include/mesp_board.h`.
- **TuyaLink**: cloud OTA not implemented; `property/report_response` does not arrive in the EU region (nor for the spike), so the cloud's acceptance of each value cannot be confirmed from the device (only the PUBACK). Verify in the app that the values arrive and that `power_on` from the app turns the PC on.
- **Power-on from S3/S4/S5 not validated** on the Lenovo (MESP-US-0002; the PC cannot be suspended in this environment). Real remote wakeup and forced resume have not been tested. WOL has not been tested with the PC off either.
- **CLI security**: resolved with the release/dev variants. Still open:
  - `!dfu` and the *1200-baud touch* allow reflashing from the PC (a conscious decision: recovery without the button).
  - NVS is not encrypted (`CONFIG_NVS_ENCRYPTION`) and there is no *secure boot* / *flash encryption*: with physical access the agent key, the deviceSecret and the Wi-Fi password can be read.
- **DP latency**: the `esp-mqtt` *outbox* is drained on each loop of its task (≤ ~1 s); on the board, the PUBACK of the first report arrives within the same second as the connection.
- **`mhal_cdc_write`** can block the app task for up to ~1.5 s per line if a program opens the port and does not read. The software watchdog (60 s) covers a total hang.
- **Internal heap** ~155 KB free with Wi-Fi, TLS and LVGL (no BLE).
- **docs/ui.md**, final typefaces and icons, night dimming and 30 fps: phase B (MESP-US-0017/0018/0020).
