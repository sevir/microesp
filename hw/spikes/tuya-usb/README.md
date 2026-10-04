# Spike MESP-US-0003 — TuyaOpen + TinyUSB (HID + CDC) on ESP32-S3

TuyaOpen app based on `apps/tuya_cloud/switch_demo` (TuyaOpen v1.9.0, ESP-IDF v5.4) that also brings up a composite USB device with `esp_tinyusb` 2.3.0. Environment and commands: `docs/dev-setup.md`.

## Verdict

**TuyaOpen can host TinyUSB without modifying the SDK.** TuyaOpen's ESP32 platform builds an ESP-IDF project (`platform/ESP32/tuya_open_sdk`) and supports the app's own ESP-IDF components in `<app>/esp_components/` (variable `TUYAOS_EXTRA_COMPONENT_DIRS`, supported by `tos.py`). The TinyUSB Kconfig options are injected with `SDKCONFIG_DEFAULTS` (see `build.sh`). No plan B is needed (own ESP-IDF project + TuyaOpen cloud libraries).

Friction points (all resolved without touching TuyaOpen code):
- The 16 MB table requires its own board (`board/POCKET_DONGLE_S3`, registered with `install-board.sh`: symbolic link + entry in `boards/ESP32/Kconfig` of the local checkout).
- `espressif/esp-sr==2.4.7` must be pinned (the latest version pulls in an esp-dl that is incompatible with IDF v5.4.0).
- The app code (`src/`) is built with TuyaOpen's CMake and does not see the ESP-IDF headers: it calls the components through `extern` prototypes; the component forces linking with `-u usb_composite_start`.

## What is here

| Path | Contents |
|---|---|
| `src/tuya_main.c` | switch_demo + `usb_composite_start()` + `pinout_probe_start()` + TuyaOpen log duplicated to a buffer |
| `esp_components/usb_composite/` | descriptors, boot-keyboard HID, CDC with echo/commands, entry to download mode, safety nets |
| `esp_components/pinout_probe/` | reuses `hw/spikes/pinout/main/main.c` (LCD pattern + LED/backlight cycle) |
| `board/POCKET_DONGLE_S3/` | TuyaOpen board (16 MB, PSRAM) |
| `sdkconfig.microesp` | `TINYUSB_CDC_ENABLED`, `HID_COUNT=1`, suspend/resume callbacks |
| `tools/flash.sh`, `tools/touch1200.py`, `tools/mesp_cdc.py` | flashing without BOOT and CDC console |
| `src/tuya_config_secrets.h` | **ignored by git**; placeholder UUID/AuthKey |

## Evidence (2026-10-03, real board)

Enumeration (`lsusb -v -d 303a:4002`, sysfs `/sys/bus/usb/devices/3-4`):

```
303a:4002 MicroESP MicroESP, serial MESP-907069f662dc, bcdDevice 1.00, USB 2.00 FS (12 Mb/s)
bmAttributes 0xa0 (Remote Wakeup), MaxPower 100mA, power/wakeup=enabled
3-4:1.0 class 03/01/01 (HID boot keyboard)  driver=usbhid
3-4:1.1 class 02/02/00 (CDC ACM)            driver=cdc_acm
3-4:1.2 class 0a       (CDC data)           driver=cdc_acm
/dev/serial/by-id/usb-MicroESP_MicroESP_MESP-907069f662dc-if01
/dev/input/by-id/usb-MicroESP_MicroESP_MESP-907069f662dc-event-kbd
```

- CDC: line echo (`hola` → `echo: hola`), 300 B lines, `!status`/`!log` commands; after binary garbage (esptool against the CDC) the parser discards the line and keeps working.
- HID: `!key` sends left Shift press/release; read on `/dev/input/event26` as `EV_KEY 42 1` / `EV_KEY 42 0`.
- `!wake` with the bus not suspended → `ESP_ERR_NOT_ALLOWED` (expected). **Real remote wakeup not tested**: it would require suspending this PC (forbidden in this environment); left for MESP-US-0002.
- TuyaOpen in parallel (log via `!log`): Wi-Fi initialized in softAP `SmartLife-62DD` (netcfg AP), BT controller enabled (`esp_bt_controller_get_status()=2`), `tuya ble init success`, `Start Adv`, `tuya_iot STATE_START`. Without valid credentials (`tuyaopen_license_read read failure`), so it does not reach the cloud: expected.
- Reflashing without BOOT verified 5 times (`!dfu` ×3, 1200-baud touch ×2) and `!usj` + normal esptool verified.

## Memory and partitions

Table (TuyaOpen's `partitions_16M.csv`, dual OTA):

| Partition | Offset | Size |
|---|---|---|
| nvs | 0x9000 | 16 KB |
| otadata | 0xd000 | 8 KB |
| phy_init | 0xf000 | 4 KB |
| ota_0 | 0x10000 | 7.4 MB (0x760000) |
| ota_1 | 0x770000 | 7.4 MB |
| model (spiffs, esp-sr) | 0xED0000 | 960 KB |
| tuya (KV) | 0xFC0000 | 240 KB |
| factory_nvs | 0xFFC000 | 16 KB |

- App image: **1.21 MB** (16% of an OTA slot; 84% free). Without `pinout_probe` the size is practically the same.
- Static (`esp_idf_size`): IRAM 16,383 / 16,384 B (**100%**), D/IRAM 142 KB used / 199 KB free, flash .text 884 KB + .rodata 195 KB.
- Runtime heap (Wi-Fi AP + BLE + TinyUSB): internal free **~115 KB** (min 110 KB); with `pinout_probe` (25 KB framebuffer in internal RAM + SPI/RMT) ~62–93 KB. PSRAM free 8.2 MB.
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is **disabled** in TuyaOpen's sdkconfig (relevant for MESP-US-0011).

## Reflashing without pressing BOOT

TinyUSB takes the PHY (`303a:1001` disappears). The firmware offers:

| Way | Effect |
|---|---|
| 1200-baud touch (`tools/touch1200.py`), `!dfu`, BOOT ≥2 s | PHY → USB-Serial/JTAG, `RTC_CNTL_FORCE_DOWNLOAD_BOOT`, `esp_restart()` → ROM in download mode on `303a:1001` |
| `!usj` | reboots once without TinyUSB (returns the PHY mux to the hardware) → esptool with normal reset |
| Not enumerated within 20 s / 3 consecutive crashes | same as `!usj` (automatic) |

Afterwards: esptool with the **default** reset sequence (`tools/flash.sh` does it all). With `--before no_reset` the final reset via RTS leaves the board in download mode again (DTR stays active); you get out by opening the port with DTR=0 and pulsing RTS, or with another normal esptool.

## Risks

- IRAM at 100%: adding code in IRAM (ISR, `IRAM_ATTR`) will fail at link time; the Wi-Fi/LWIP `*_IRAM_OPT` options would have to be disabled.
- TuyaOpen's console goes to UART0 (GPIO43/44, not accessible) and to the USJ, which disappears with TinyUSB: logs are only visible with `!log` (64 KB buffer in PSRAM). For the real firmware a second CDC or multiplexing logs into the protocol is advisable.
- The board and the esp-sr pin live outside the TuyaOpen tree but modify the local checkout (`install-board.sh`); when updating TuyaOpen it has to be rerun.
- If the board ends up in a state without USB (e.g. a lock-up with the PHY in OTG), the only recourse is BOOT while plugging in.
