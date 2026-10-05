# MicroESP development environment (MESP-US-0005)

Verified on Linux (Pop!_OS / Ubuntu 24.04, x86_64) on 2026-10-03, without `sudo`. Everything is installed in user space under `/www/MicroESP/tools`.

## 1. Versions

| Component | Version | Location |
|---|---|---|
| TuyaOpen | **v1.9.0** (commit `b80932d`) | `/www/MicroESP/tools/TuyaOpen` |
| TuyaOpen Python environment | uv 0.11.18 + Python 3.12.13 (`.venv/`) | inside TuyaOpen |
| TuyaOpen ESP32 platform | `TuyaOpen-esp32` commit `e2b4b26` | `TuyaOpen/platform/ESP32` |
| ESP-IDF (downloaded by TuyaOpen) | **v5.4** (tag `67c1de1e`; shows as `v5.4-dirty` because TuyaOpen replaces `tools/idf_tools.py`) | `TuyaOpen/platform/ESP32/esp-idf` |
| IDF tools | xtensa-esp-elf-gcc 14.2.0 (esp-14.2.0_20241119), ninja 1.12.1, esptool.py 4.12.0 | `TuyaOpen/platform/ESP32/.espressif` |
| IDF components used | esp_tinyusb 2.3.0, tinyusb 0.21.0~2, led_strip 3.0.3, esp-sr **2.4.7 (pinned)** | component manager |
| Go | 1.26.6 (already installed on the system) | — |
| Standalone esptool (optional) | 5.4.0 in its own venv | any venv |

Disk space: ~2 GB TuyaOpen + 2 GB esp-idf + 3.8 GB `.espressif` tools.

## 2. Installation

```bash
mkdir -p /www/MicroESP/tools && cd /www/MicroESP/tools
git clone --depth 1 --branch v1.9.0 https://github.com/tuya/TuyaOpen
cd TuyaOpen
mkdir -p .cache && touch .cache/.dont_prompt_update_platform   # avoids interactive prompts
. ./export.sh            # creates .venv with uv, installs Python 3.12.13, puts tos.py on the PATH
tos.py check             # git/cmake/make/ninja + submodules
```

The ESP32 platform and its ESP-IDF are downloaded automatically on the first `tos.py build` of an ESP32 app (it clones `TuyaOpen-esp32`, ESP-IDF v5.4 with submodules and runs `install.sh esp32s3`). It takes ~10–15 min.

Environment scripts (created in `/www/MicroESP/tools`):

```bash
source /www/MicroESP/tools/tos-env.sh   # TuyaOpen environment (tos.py)
source /www/MicroESP/tools/idf-env.sh   # "bare" ESP-IDF reusing TuyaOpen's (idf.py)
```

`idf-env.sh` exports `IDF_PATH=/www/MicroESP/tools/TuyaOpen/platform/ESP32/esp-idf` and `IDF_TOOLS_PATH=…/platform/ESP32/.espressif`. ninja was not among the tools installed by TuyaOpen; it was added with:

```bash
source /www/MicroESP/tools/idf-env.sh
python $IDF_PATH/tools/idf_tools.py install ninja
```

Do not mix the two environments in the same shell: use one per terminal (or subshell).

### Problems found and fixes

1. **esp-sr / esp-dl incompatible with IDF v5.4.0**: TuyaOpen asks for `espressif/esp-sr ^2.0.0` without a lock file; today it resolves to esp-sr 2.5.5 → esp-dl 3.3.x, which uses `MALLOC_CAP_SIMD` (it does not exist in IDF v5.4.0) and the build fails. Fix: the app component pins `espressif/esp-sr: "==2.4.7"` (`hw/spikes/tuya-usb/esp_components/usb_composite/idf_component.yml`). If it is changed, delete `TuyaOpen/platform/ESP32/tuya_open_sdk/dependencies.lock`.
2. **Flash size**: TuyaOpen's generic `ESP32-S3` board uses the 4 MB table. For the 16 MB one (7.4 MB dual OTA) a board that selects `PLATFORM_FLASHSIZE_16M` is needed. The `POCKET_DONGLE_S3` board was created (source in `hw/spikes/tuya-usb/board/`), registered in the TuyaOpen checkout with `hw/spikes/tuya-usb/install-board.sh` (symbolic link + entry in `boards/ESP32/Kconfig`; idempotent, `build.sh` runs it).
3. **sdkconfig**: TuyaOpen copies a fixed `sdkconfig` per chip (`sdkconfig_esp32s3_uart`). Custom options (TinyUSB) are added through the standard ESP-IDF environment variable `SDKCONFIG_DEFAULTS="<platform>/sdkconfig.defaults;<app>/sdkconfig.microesp"`, which `build.sh` exports. After editing `sdkconfig.microesp`, build with `./build.sh clean`.
4. `idf.py size` inside `tuya_open_sdk/` reconfigures without the TuyaOpen environment: do not use it. To measure memory: `python -m esp_idf_size dist/tuya-usb_1.0.0/tuya-usb_1.0.0.map` (IDF environment).

## 3. Build

TuyaOpen + TinyUSB spike (TuyaOpen app with its own ESP-IDF components in `esp_components/`):

```bash
cd /www/MicroESP/microesp/hw/spikes/tuya-usb
./build.sh          # incremental;  ./build.sh clean  after touching sdkconfig.microesp or the board
# output: dist/tuya-usb_1.0.0/{bootloader.bin,partition-table.bin,ota_data_initial.bin,tuya-usb.bin,srmodels.bin,tuya-usb_QIO_1.0.0.bin}
```

Credentials go in `src/tuya_config_secrets.h` (ignored by git; `build.sh` creates it from `.example` with placeholder values).

Pinout spike (plain ESP-IDF):

```bash
source /www/MicroESP/tools/idf-env.sh
cd /www/MicroESP/microesp/hw/spikes/pinout
idf.py set-target esp32s3   # first time only
idf.py build
```

## 4. Serial port access (`sg dialout`)

The user is already in the `dialout` group (`scripts/setup-serial-access.sh`), but shells opened before that do not have it active. Until you log in again, wrap **every** command that opens the port:

```bash
sg dialout -c "command ..."
```

Ports (stable by serial number):

| Dongle state | USB | Port |
|---|---|---|
| Firmware with TinyUSB (MicroESP) | `303a:4002` | `/dev/serial/by-id/usb-MicroESP_MicroESP_MESP-<mac>-if01` (CDC) |
| USB-Serial/JTAG (firmware without TinyUSB, or ROM in download mode) | `303a:1001` | `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<MAC>-if00` |

## 5. Flashing

On the live machine (agent installed), prefer the wrappers in `scripts/` (`dongle-flash.sh`, `dongle-cli.sh`, `agent-reinstall.sh`): they stop and restart the agent and handle `dialout`. See [`operations/live-scripts.md`](operations/live-scripts.md).

### 5.1 Firmware without TinyUSB (USB-Serial/JTAG active)

```bash
sg dialout -c "bash -c 'source /www/MicroESP/tools/idf-env.sh; cd /www/MicroESP/microesp/hw/spikes/pinout; idf.py -p /dev/ttyACM0 flash monitor'"
```

### 5.2 Firmware with TinyUSB (without pressing BOOT) — verified

With TinyUSB active the USB PHY is used by USB-OTG and USB-Serial/JTAG disappears. The firmware enters **ROM download mode** in any of these ways:

- *1200-baud touch*: open the CDC at 1200 bps and lower DTR (`hw/spikes/tuya-usb/tools/touch1200.py`).
- The `!dfu` command over the CDC.
- Hold BOOT for ≥2 s with the firmware running.

The firmware gives the PHY back to USB-Serial/JTAG, writes `RTC_CNTL_FORCE_DOWNLOAD_BOOT` and reboots: the dongle reappears as `303a:1001` in download mode. Then flash with esptool's **default** reset sequence (with `--before no_reset` the final reset leaves it in download mode again). All together:

```bash
sg dialout -c /www/MicroESP/microesp/hw/spikes/tuya-usb/tools/flash.sh
```

Other CDC commands: `!usj` (reboots **once** without TinyUSB → regular esptool works), `!status`, `!log`, `!key`, `!wake`, `!reboot`.

```bash
sg dialout -c "bash -c 'source /www/MicroESP/tools/idf-env.sh; python /www/MicroESP/microesp/hw/spikes/tuya-usb/tools/mesp_cdc.py !status !log'"
```

Firmware safety nets: if the host does not enumerate it within 20 s, or after 3 consecutive crash reboots, it boots without TinyUSB (USB-Serial/JTAG available).

### 5.3 Manual download mode (last resort)

1. Unplug the dongle.
2. Hold **BOOT** (GPIO0).
3. Plug in the dongle without releasing BOOT; release after 1 s.
4. `303a:1001` appears; flash with esptool or `idf.py flash`.
5. Unplug and plug in again (or esptool reset) to boot the firmware.

## 6. Restore the factory firmware

Full 16 MB backup: `hw/factory-backup/pocket-dongle-s3_factory_16MB.bin` (check from the repo root with `sha256sum -c hw/factory-backup/SHA256SUMS`).

```bash
# If firmware with TinyUSB is running, put it in download mode first (1200 touch / !dfu / BOOT while plugging in)
sg dialout -c "bash -c 'source /www/MicroESP/tools/idf-env.sh; python -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 921600 write_flash 0x0 /www/MicroESP/microesp/hw/factory-backup/pocket-dongle-s3_factory_16MB.bin'"
```

Writing the full image also restores the original NVS and partition table.
