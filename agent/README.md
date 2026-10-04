# microesp-agent

Resident agent on the PC for the **MicroESP** USB dongle. It talks to the dongle over the CDC port (`/dev/ttyACM*`) using the [`cdc-v1`](../docs/protocol/cdc-v1.md) protocol and:

- sends the PC **telemetry**: CPU %, used memory %, lowest free % of the configured disks, uptime, hostname and MACs of the physical NICs (for Wake-on-LAN);
- keeps a **heartbeat** so the dongle knows the agent is alive;
- runs the **signed** shutdown and reboot commands that arrive from the Tuya app through the dongle.

Every command is verified (HMAC-SHA256 with the pairing key and the session nonces, increasing id against replays, known action). The agent answers with `ack` **before** running it.

## Requirements

- Linux with systemd and polkit (tested on Ubuntu/Pop!_OS 24.04), or Windows 10/11 (optional).
- Dongle with MicroESP firmware (USB `303a:4002`, product `MicroESP`).
- Go ≥ 1.23 only if building from source.

## Build

```sh
make build             # ./microesp-agent (version from git describe)
make build VERSION=1.0.0
make cross             # dist/: linux-amd64, linux-arm64, windows-amd64.exe
make test lint cover   # tests with -race, go vet, staticcheck, shellcheck, coverage ≥70 %
```

Releases are generated with `goreleaser release` (`.goreleaser.yaml`). Each archive includes the binary, this README and `deploy/`.

## Installation on Linux

```sh
sudo ./deploy/install.sh                  # builds (or uses ../microesp-agent) and installs
sudo ./deploy/install.sh --binary ./microesp-agent
./deploy/install.sh --dry-run             # only shows what it would do (does not require root)
```

The installer is idempotent; you can run it again to update. It does the following:

| Step | Result |
|---|---|
| system user | `microesp` (no shell or home), member of `dialout` |
| binary | `/usr/local/bin/microesp-agent` |
| configuration | `/etc/microesp/agent.toml` (not overwritten if it already exists), directory `root:microesp 0750` |
| udev | `/etc/udev/rules.d/99-microesp.rules`: group `dialout`, symlink `/dev/microesp`, ModemManager ignores the port, `power/wakeup=enabled` so the dongle's HID keyboard can wake the PC |
| polkit | `/etc/polkit-1/rules.d/50-microesp.rules`: the `microesp` user can only shut down or reboot (`org.freedesktop.login1.power-off`, `power-off-multiple-sessions`, `reboot`, `reboot-multiple-sessions`) |
| systemd | `/etc/systemd/system/microesp-agent.service` enabled and started |

The unit runs without privileges and with a strict sandbox (`ProtectSystem=strict`, `DevicePolicy=closed` + `DeviceAllow=char-ttyACM rw`, no IP network, `NoNewPrivileges`). `systemd-analyze security microesp-agent` gives an exposure of **1.2**.

To uninstall:

```sh
sudo ./deploy/install.sh --uninstall           # keeps /etc/microesp (config and key)
sudo ./deploy/install.sh --uninstall --purge   # removes everything
```

## Pairing with the dongle

If the dongle has no key, or if you hold its button for 3 s, it shows a 6-digit code on the screen for 120 s.

```sh
sudo systemctl stop microesp-agent        # the serial port is opened exclusively
sudo microesp-agent pair                  # asks for the code (or: --code 123456)
sudo systemctl start microesp-agent
```

`pair` derives the key with HKDF-SHA256 from the code and the nonces of both sides, and stores it in `/etc/microesp/agent.key` (hex, mode `0600`, owner `microesp`) only after verifying the dongle's `pair_ok` response. If you pair again, the previous key is replaced on both sides. After 3 wrong codes the dongle leaves pairing mode.

## Configuration

TOML file (by default `/etc/microesp/agent.toml`; on Windows `C:\ProgramData\MicroESP\agent.toml`). A commented example is in [`deploy/agent.toml.example`](deploy/agent.toml.example).

| Key | Default | Environment variable | Flag |
|---|---|---|---|
| `device` | `"auto"` | `MICROESP_DEVICE` | `--device` |
| `key_file` | `/etc/microesp/agent.key` | `MICROESP_KEY_FILE` | `--key-file` |
| `disks` | `["/"]` | `MICROESP_DISKS` (comma-separated list) | — |
| `telemetry_interval` | `"10s"` | `MICROESP_TELEMETRY_INTERVAL` | — |
| `heartbeat_interval` | `"5s"` | `MICROESP_HEARTBEAT_INTERVAL` | — |
| `dry_run` | `false` | `MICROESP_DRY_RUN` | `--dry-run` |
| `power_backend` | `"systemd"` (`"windows"` on Windows) | `MICROESP_POWER_BACKEND` | `--power-backend` |
| `log_level` | `"info"` | `MICROESP_LOG_LEVEL` | `--log-level` |

Precedence: defaults < file < environment < flags. If you do not pass `--config` and the default file does not exist, the defaults are used. An unknown key is an error.

- **device**: with `auto` it looks for a port with VID `303a` and PID `4002` (or whose product contains `MicroESP`). If none shows up, it searches again every 2 s. Open or handshake failures are retried with exponential backoff from 1 s to 30 s.
- **Telemetry**: CPU is measured over the window between two samples (one per `heartbeat_interval`). Used memory excludes cache and buffers. For each disk the free % is computed as `df` does and the lowest one is reported. Values are sent every `telemetry_interval`, or earlier if any changes by more than 2 points (20 tenths).
- **power_backend**:
  - `systemd` runs `systemctl poweroff|reboot`, which requests the action from logind over D-Bus and is authorized by the polkit rule.
  - `logind-dbus` calls `org.freedesktop.login1.Manager.PowerOff/Reboot` directly through `busctl`.
  - `windows` runs `shutdown /s|/r /t 0`.
- **dry_run**: logs the commands instead of running them. Use it for testing.

## Usage

```sh
microesp-agent                 # = run
microesp-agent run --dry-run --log-level debug
microesp-agent status          # detected ports, key and backend state
microesp-agent status --handshake   # also opens the port and authenticates (stop the service first)
microesp-agent version
journalctl -u microesp-agent -f
```

## Windows (optional)

```powershell
# PowerShell as Administrator, next to microesp-agent.exe
.\deploy\windows\install.ps1 -Binary .\microesp-agent.exe
Stop-Service MicroESPAgent; & "$env:ProgramFiles\MicroESP\microesp-agent.exe" pair --config "$env:ProgramData\MicroESP\agent.toml"; Start-Service MicroESPAgent
.\deploy\windows\install.ps1 -Uninstall [-Purge]
```

The `MicroESPAgent` service runs as LocalSystem and is integrated with the SCM through `kardianos/service`. The configuration and key are stored in `C:\ProgramData\MicroESP`, with permissions restricted to SYSTEM and Administrators.

## Troubleshooting

| Symptom | Probable cause / solution |
|---|---|
| `dongle not found` in the log | The dongle is not plugged in or is not running the MicroESP firmware. Check `lsusb \| grep 303a` and that `/dev/microesp` appears. |
| `open /dev/ttyACM0: permission denied` | The udev rule is missing or the user is not in `dialout`. Run `sudo udevadm trigger` and check `ls -l /dev/ttyACM*`. |
| `device or resource busy` when running `pair` or `status --handshake` | The service has the port open exclusively. Stop the service first with `sudo systemctl stop microesp-agent`. |
| `dongle error: not_paired` | Pair with `microesp-agent pair`. |
| `welcome signature invalid` | The key does not match (the dongle was paired with another machine) or the device is not genuine. Pair again. |
| `load key ... permissions too open` | Run `sudo chmod 600 /etc/microesp/agent.key && sudo chown microesp /etc/microesp/agent.key`. |
| `power action failed ... Access denied` / `interactive authentication required` | The polkit rule is missing or an **inhibitor** is active (for example, an update in progress; see it with `systemd-inhibit --list`). By design, the agent cannot bypass inhibitors. |
| Ack `exec_failed` in the app | The backend binary (`systemctl`, `busctl` or `shutdown`) was not found. |
| ModemManager sends `AT` commands to the dongle | The udev rule (`ID_MM_DEVICE_IGNORE`) is missing. |
| Risk-free testing | Start with `microesp-agent run --dry-run --log-level debug`. |

Countdown notices (`notice`) are attempted to be broadcast with `wall`, but the systemd sandbox normally prevents it. That is fine: the countdown is visible on the dongle screen and in the app.

## Development

- `internal/proto`: messages, framing (512 B per line) and HMAC/HKDF. The tests use the normative vectors in `../protocol/testdata/vectors.json`.
- `internal/link`: discovery, session, heartbeat, reconnection and command rules. The tests use `net.Pipe` and a simulated dongle (`internal/link/dongletest`).
- `internal/telemetry`: sampling with gopsutil and the `Collector` interface (includes a fake implementation).
- `internal/power`: `Executor` interface, with systemd, logind-dbus, Windows and DryRun backends. The tests never run a real action.
- `internal/config`: TOML, environment and key file.
