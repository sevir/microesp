# MicroESP installation guide

Story: MESP-US-0034. End-to-end steps to get a MicroESP dongle working with the PC: flash the firmware, load the TuyaLink credentials and the Wi-Fi, check that it shows up in Smart Life, install the agent, pair the agent with the dongle and verify the whole setup.

You need:

- The Pocket-Dongle-S3 dongle (ESP32-S3 with 16 MB of flash, ST7735 display).
- The target PC running Linux with systemd (tested on Pop!_OS / Ubuntu 24.04). Windows is optional: see [`agent/README.md`](../../agent/README.md#windows-optional).
- An account in the **Smart Life** app (or Tuya Smart) and a **2.4 GHz** Wi-Fi network (with a password; the SSID cannot contain spaces).
- The device's **TuyaLink** credentials, which the Tuya platform (platform.tuya.com) gives you when you create the device in the MicroESP product: **region** (`eu`, `us`, `cn` or `in`), **productId**, **deviceId** and **deviceSecret**. The device is linked to your app account from the platform. The deviceSecret is secret: do not publish it or commit it to the repository. (Since version 0.2.0, TuyaOS UUID/AuthKey licenses are no longer needed.)
- Administrator access (`sudo`) on the PC.

> If you build from source, set up the environment first with [`docs/dev-setup.md`](../dev-setup.md).

## 0. Back up the factory firmware (once)

Before the first flash, save the full original flash (16 MB) so you can go back. Put the dongle in download mode (hold **BOOT** while plugging it in) and run:

```bash
python -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 921600 read_flash 0x0 0x1000000 pocket-dongle-s3_factory_16MB.bin
sha256sum pocket-dongle-s3_factory_16MB.bin > SHA256SUMS
```

Keep it outside the repository (git ignores `hw/factory-backup/*.bin`). To restore it, follow [`docs/dev-setup.md` §6](../dev-setup.md#6-restore-the-factory-firmware).

## 1. Flash the firmware

Your user must belong to the `dialout` group (`scripts/setup-serial-access.sh`). If you have just added it, prefix the commands with `sg dialout -c "..."` until you log in again.

### Option A: from a release

From the `vX.Y.Z` release, download the file `microesp-firmware_X.Y.Z_merged.bin` and `SHA256SUMS`, and check the checksum:

```bash
sha256sum -c --ignore-missing SHA256SUMS
```

Put the dongle in download mode. The first time, hold **BOOT** while plugging it in. If it already runs MicroESP, `!dfu`, the *1200-baud touch* or holding BOOT for 20 s or more also work. Then:

```bash
python -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 921600 write_flash 0x0 microesp-firmware_X.Y.Z_merged.bin
```

Unplug the dongle and plug it in again.

### Option B: from source

```bash
cd firmware
./build.sh
sg dialout -c tools/flash.sh      # without pressing BOOT if MicroESP is already running; otherwise BOOT while plugging in
```

**Check:** `lsusb | grep 303a:4002` shows the `MicroESP` device, and `/dev/serial/by-id/usb-MicroESP_MicroESP_MESP-*-if01` appears. The display shows the status and the LED blinks blue because the dongle is not provisioned yet.

## 2. Load the TuyaLink credentials and the Wi-Fi

They are loaded through the CDC port CLI and stored in NVS (they are not left in any file). Flashing again does not erase them. If the agent is already installed, stop it first (`sudo systemctl stop microesp-agent`), because it opens the port exclusively.

```bash
source /www/MicroESP/tools/idf-env.sh        # or any Python with pyserial
sg dialout -c "python firmware/tools/mesp_cdc.py '!tylink <region> <productId> <deviceId> <deviceSecret>'"
sg dialout -c "python firmware/tools/mesp_cdc.py '!wifi <ssid> <password>' '!reboot'"
```

- The Wi-Fi password is **the rest of the line** after the SSID: it may contain spaces.
- To keep the secret out of the shell history, you can type the commands in a serial terminal (`python -m serial.tools.miniterm /dev/serial/by-id/usb-MicroESP_*-if01`) instead of passing them as arguments.
- In the release build, `!tylink` and `!wifi` are only accepted the first time (value not yet stored). To change them later, **hold the button for 5 to 10 s and release it**: a 120 s window opens ("Aprovisionar 120 s" on the display, i.e. "Provision 120 s").
- The dongle never shows the deviceSecret or the password: neither in `!status` nor in the log.

**Check** (about 10 s after the reboot): `mesp_cdc.py '!status'` shows

```
tylink: region=eu host=m1.tuyaeu.com product=<productId> device=26e0...0z provisioned=1 mqtt=connected ...
wifi: ssid=<ssid> configured=1 up=1 ip=192.168.x.y rssi=-58 ... time_synced=1
```

The LED stops blinking blue and the cloud icon on the display appears without a strike-through.

## 3. Check the device in Smart Life

With TuyaLink there is **no BLE/AP pairing**. Steps (validated on 2026-10-04 in Central Europe):

1. On the Tuya platform, the product must be **TuyaLink** (not TuyaOS) and must be in the **same data center** as your Smart Life account (in Spain: Central Europe).
2. Register the device in the product's **Device Management**: you get `productId`, `deviceId` and `deviceSecret` (the ones loaded with `!tylink`).
3. With the dongle **connected** (`!status` → `mqtt=connected`), open the **device QR** in Device Management and in Smart Life tap **+ → Scan** (not "Add device"). If the dongle is disconnected the linking fails.
4. If in the app you only see the category control (e.g. a plug) and not CPU/memory/status, change the product's **panel** on the platform to one that shows all the functions.

Notes:
- The EU cloud accepts reports without acknowledging them (`property/report_response` only arrives if the message asks for `"sys":{"ack":1}`).
- Commands from the app arrive as `property/set` with a numeric `msgId`.

There can only be **one connection per deviceId**: if another program uses the same credentials (for example the test script `hw/spikes/tylink_test.py`), the cloud disconnects the dongle, which retries on its own.

## 4. Install the agent on the PC

From a release (tarball `microesp-agent_X.Y.Z_linux_amd64.tar.gz`):

```bash
mkdir microesp-agent && tar xzf microesp-agent_X.Y.Z_linux_amd64.tar.gz -C microesp-agent && cd microesp-agent
sudo ./deploy/install.sh --binary ./microesp-agent
```

From source (needs Go ≥ 1.23):

```bash
sudo agent/deploy/install.sh          # builds and installs; idempotent
./agent/deploy/install.sh --dry-run   # only shows what it would do
```

The installer creates the `microesp` user, installs the binary in `/usr/local/bin`, the configuration in `/etc/microesp/agent.toml`, the udev rule (port permissions, `/dev/microesp` and `power/wakeup`), the polkit rule (power off and reboot only) and the `microesp-agent` service. Details are in [`agent/README.md`](../../agent/README.md).

## 5. Pair the agent with the dongle

1. Put the dongle in pairing mode: **hold the button for 3 s and release it**. If the dongle has no key, it enters this mode on its own at boot. The display shows a 6-digit code for 120 s.
2. On the PC:
   ```bash
   sudo systemctl stop microesp-agent
   sudo microesp-agent pair              # asks for the code; or --code 123456
   sudo systemctl start microesp-agent
   ```
3. After 3 wrong codes the dongle leaves pairing mode. Go back to step 1.

## 6. Configure the BIOS and the system for power-on

Follow [`bios-lenovo.md`](bios-lenovo.md): ErP disabled, Wake on LAN enabled, USB powered in S4/S5, WOL on the NIC (`nmcli ... 802-3-ethernet.wake-on-lan magic`) and the dongle's `power/wakeup`.

## 7. Verification

| Check | How | Expected |
|---|---|---|
| Service active | `systemctl status microesp-agent`, `journalctl -u microesp-agent -n 50` | `active (running)`, session `ready` in the log |
| Agent status | `sudo microesp-agent status` | dongle detected, key present, `systemd` backend |
| App | Smart Life | `pc_state = on`, `agent_online = true`, CPU/MEM/disk and hostname updated |
| Risk-free shutdown | In `/etc/microesp/agent.toml` set `dry_run = true`, restart the service and tap **Shut down** in the app | Countdown on the dongle; in the log, the received command (not executed); `last_result = ok` |
| Cancellation | Tap **Shut down** and, during the countdown, short-press the button | `last_result = cancelled` |
| Power on | [`bios-lenovo.md` §4](bios-lenovo.md#4-how-to-test-it-safely-s3-first-then-s5) | The PC wakes from S3 (and from S5 if the BIOS allows it) |

When everything works, set `dry_run = false` again. The full test plan is in [`docs/qa/e2e-v1.md`](../qa/e2e-v1.md).

## 8. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `303a:4002` does not appear after flashing | The firmware booted without TinyUSB (safety net: it did not enumerate within 20 s or there were 3 consecutive crashes). It appears as `303a:1001`. Unplug, plug in again and check `!log`. |
| `esptool` does not connect | The dongle is not in download mode. Hold BOOT while plugging it in. Do not use `--before no_reset`. |
| `permission denied` on `/dev/ttyACM*` | The user is not in `dialout`, or the udev rule has not been applied: `sudo udevadm trigger` or `sg dialout -c ...`. |
| `device or resource busy` | The agent has the port open: `sudo systemctl stop microesp-agent`. |
| The dongle does not show up in Smart Life | Check `!status`. `wifi: ... up=0`: wrong SSID or password, or a 5 GHz network (`last_reason` gives the Wi-Fi reason). `time_synced=0`: the network blocks NTP. `mqtt=connecting` with `last_err=4` or `5` (CONNACK code): TuyaLink credentials rejected (repeat `!tylink` using the 5 s button window). `last_err=-1`: no TLS connection to the broker (firewall, port 8883). If everything is `connected`, check on the platform that the device is linked to your account. |
| `cloud_lost` (bit 3 of DP 114), red LED or struck-through cloud icons | 60 s without an MQTT session: same as the previous row. It also happens if another client uses the same deviceId. |
| `dongle error: not_paired` / `welcome signature invalid` | Pair again (§5). The previous key is replaced on both sides. |
| `agent_offline` when powering off | The agent is not connected: `systemctl status microesp-agent`. |
| `cmd_rejected` | The agent rejected the command or did not confirm it within 10 s. Check `journalctl -u microesp-agent` for `bad_sig`/`replay`. |
| `Access denied` / `interactive authentication required` when powering off | The polkit rule is missing, or an inhibitor is active (`systemd-inhibit --list`). |
| `hid_not_armed` (bit 2 of DP 114) | The host suspended USB without arming remote wakeup: check `power/wakeup` ([`bios-lenovo.md` §3.1](bios-lenovo.md#31-allow-the-dongle-to-wake-the-computer-s3)). |
| `wake_failed` | Within 120 s the agent neither connected nor did the PC enumerate the dongle again. Check the BIOS (ErP, Always On USB), the NIC's WOL and that the dongle has the MACs (`macs=` in `!status`). |
| ModemManager sends `AT` to the dongle | The udev rule (`ID_MM_DEVICE_IGNORE`) is missing. Reinstall the agent. |
| Full recovery | Restore the factory firmware ([`docs/dev-setup.md` §6](../dev-setup.md#6-restore-the-factory-firmware)). |

More agent cases in [`agent/README.md`](../../agent/README.md#troubleshooting). CLI commands and LED meaning in [`firmware/README.md`](../../firmware/README.md).
