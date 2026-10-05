# microesp-agent

Resident agent on the PC for the **MicroESP** USB dongle. It talks to the dongle over the CDC port (`/dev/ttyACM*`) using the [`cdc-v1`](../docs/protocol/cdc-v1.md) protocol and:

- sends the PC **telemetry**: CPU %, used memory %, lowest free % of the configured disks, uptime, hostname and MACs of the physical NICs (for Wake-on-LAN);
- keeps a **heartbeat** so the dongle knows the agent is alive;
- runs the **signed** shutdown and reboot commands that arrive from the Tuya app through the dongle;
- runs up to 5 **user scripts** defined in its configuration, also on a signed command from the app ([User scripts](#user-scripts)).

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
sudo ./deploy/install.sh --scripts-user "$USER"   # also run user scripts as you (see User scripts)
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
| scripts runner (only with `--scripts-user NAME`) | `microesp-scripts.socket` + `microesp-scripts.service` for user `NAME`, socket enabled, and `scripts_socket = "/run/microesp/scripts.sock"` set in `agent.toml` ([User scripts](#user-scripts)) |

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
| `scripts_socket` | `""` (scripts run in the agent) | — | — |
| `[[scripts]]` | none | — | — |

Precedence: defaults < file < environment < flags. If you do not pass `--config` and the default file does not exist, the defaults are used. An unknown key is an error.

- **device**: with `auto` it looks for a port with VID `303a` and PID `4002` (or whose product contains `MicroESP`). If none shows up, it searches again every 2 s. Open or handshake failures are retried with exponential backoff from 1 s to 30 s.
- **Telemetry**: CPU is measured over the window between two samples (one per `heartbeat_interval`). Used memory excludes cache and buffers. For each disk the free % is computed as `df` does and the lowest one is reported. Values are sent every `telemetry_interval`, or earlier if any changes by more than 2 points (20 tenths).
- **power_backend**:
  - `systemd` runs `systemctl poweroff|reboot`, which requests the action from logind over D-Bus and is authorized by the polkit rule.
  - `logind-dbus` calls `org.freedesktop.login1.Manager.PowerOff/Reboot` directly through `busctl`.
  - `windows` runs `shutdown /s|/r /t 0`.
- **dry_run**: logs the commands (power actions and user scripts) instead of running them. Use it for testing.

## User scripts

Up to 5 commands that the owner of the PC allows to be started from the Tuya app. Each one is a `[[scripts]]` table in `agent.toml`:

```toml
[[scripts]]
id = "backup"            # ^[a-z0-9_-]{1,12}$, unique
label = "Backup NAS"     # shown in the app: 1..24 bytes UTF-8, no control chars, no " or \
command = "/usr/local/bin/backup.sh --full"
timeout = "30m"          # optional, default "10m"
```

- After every session `ready` the agent sends the list of `id` + `label` to the dongle (`scripts` message, [cdc-v1 §3.1](../docs/protocol/cdc-v1.md)). **The command line never leaves the PC**: the cloud can only start scripts defined here. A dongle firmware without scripts support answers `err{bad_msg}`; the agent logs it and carries on.
- `cmd{action:"script:<id>"}` goes through the same checks as shutdown/reboot (signature, increasing id). Unknown id → `ack unknown_action`; the same script still running → `ack exec_failed`; otherwise `ack ok` and the script runs **in the background** (telemetry, heartbeats and other commands keep going). Different scripts may run at the same time.
- The command runs with `sh -c` on Linux and `cmd /C` on Windows, with the environment of the process that runs it plus `MICROESP_SCRIPT_ID`, stdin closed and working directory `/`. There is no per-script user: if a script needs another user, use `sudo` inside it.
- On timeout the **whole process tree** is killed: on Linux the script runs in its own process group and the group gets `SIGKILL`; on Windows `taskkill /T /F`.
- The log gets the start (pid), exit code, duration and the last 2 KB of the combined stdout/stderr.
- With `dry_run = true` (or `--dry-run`) scripts are only logged. `microesp-agent status` shows the configured ids and where they run (`local` or `via runner <socket>`).

### Where scripts run (Linux)

**Local (default, `scripts_socket` empty).** The agent runs the scripts itself, as `microesp` inside the service sandbox. This is very limited: no network, a read-only filesystem except a private `/tmp`, no `/home`, no `sudo`. It is enough for a script that only reads local state.

**Scripts runner (`install.sh --scripts-user NAME`).** The agent service is unchanged (user `microesp`, full sandbox, same polkit rule). A small separate service runs the scripts as your desktop user, with no sandbox:

```
 Tuya app ─► dongle ─► cmd script:backup (signed) ─► microesp-agent.service
                                                     user microesp, sandboxed
                                                         │ {"id":"backup"}
                                                         ▼
                                    /run/microesp/scripts.sock (NAME:microesp 0660)
                                                         │ SO_PEERCRED: microesp or root only
                                                         ▼
                                    microesp-scripts.service (socket-activated)
                                    user NAME, no sandbox ─► sh -c "<command from agent.toml>"
```

```sh
sudo ./deploy/install.sh --scripts-user alice     # or: sudo MICROESP_SCRIPTS_USER=alice ./deploy/install.sh
```

The installer checks that `alice` exists and is not `root` or `microesp`. It installs `microesp-scripts.socket` and `microesp-scripts.service` for her, enables the socket, and sets `scripts_socket = "/run/microesp/scripts.sock"` in `agent.toml` (it updates the key if present and keeps the rest of the file). A reinstall without the flag keeps an installed runner and its user. `--uninstall` also removes the runner units.

- On `cmd script:<id>` the agent checks the id against its own list, then asks the runner **before** acking: started → `ack ok`; unknown id → `unknown_action`; already running → `exec_failed`; runner unreachable or no answer within 2 s → `exec_failed`, with an error in the agent log.
- The wire protocol is one JSON line each way per connection: `{"id":"backup"}` → `{"ok":true}` or `{"ok":false,"err":"unknown"|"busy"|"bad_request"}` (lines up to 256 bytes, 2 s read/write timeouts).
- The runner (`microesp-agent scripts-runner --config /etc/microesp/agent.toml`) loads the same config with the same validation. It belongs to the `microesp` group to read `agent.toml` (`root:microesp 0640`); the key file (`microesp 0600`) stays unreadable to it. It takes the socket from systemd (`LISTEN_FDS`), or from `--listen PATH` for manual runs.
- The agent's sandbox needs no change: `connect()` to a unix socket is allowed on a read-only mount (`ProtectSystem=strict`, `ReadOnlyPaths=/`), and `RestrictAddressFamilies` already includes `AF_UNIX`.
- The runner's output and script results are in `journalctl -u microesp-scripts`. Restart the runner after editing `[[scripts]]` (`sudo systemctl restart microesp-scripts.service`), and the agent too, so both use the same list.
- Scripts are not part of the desktop session: they get `HOME`, `USER` and `PATH` from systemd but no `DISPLAY`/`WAYLAND_DISPLAY` or session D-Bus, so GUI programs will not show up.
- A script keeps running if the agent loses the dongle, or if the agent itself restarts while the runner is in use. Stopping the process that runs it (the agent in local mode, the runner otherwise) or shutting down kills the scripts still running; it waits up to 10 s for them.

### Security

- **Only the ids in the root-owned `agent.toml` can run.** The dongle and the agent only send an id; the runner looks up the command in its own copy of the config. Neither the cloud nor the agent can send a command line.
- Whoever can edit `agent.toml` can run commands as the scripts user. So the agent and the runner refuse to start when the file defines scripts and is writable by group or others (Linux; fix with `chmod go-w`). Keep the scripts themselves writable only by their owner too.
- Only the agent's user (`microesp`) or root can talk to the runner: the socket is `NAME:microesp 0660` and the runner checks the peer uid with `SO_PEERCRED`.
- The polkit rule is unchanged: only `microesp` may shut down or reboot without a password.

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

On Windows there is no scripts runner (`scripts-runner` is Linux only; leave `scripts_socket` empty): user scripts run in the service, as **LocalSystem in session 0**: they have full rights on the machine but no access to the logged-in user's desktop (a GUI program will not be visible) nor to their mapped drives or credentials. The config file permission check is skipped on Windows; the installer's ACL on `C:\ProgramData\MicroESP` is what protects it.

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
| `config file is writable by group or others; refusing to load [[scripts]]` | `sudo chmod go-w /etc/microesp/agent.toml`. |
| Log `dongle rejected the scripts list` | The dongle firmware predates user scripts. Update the firmware; everything else keeps working. |
| A script works in a terminal but not from the app | By default it runs as `microesp` inside the agent's sandbox (no network, read-only filesystem). Install the runner with `--scripts-user` and check the output tail in `journalctl -u microesp-scripts`. |
| Ack `exec_failed` and `scripts runner failed` in the agent log | The runner socket is not up: `systemctl status microesp-scripts.socket`. |
| Log `scripts runner does not know the script` | `agent.toml` changed after the runner started: `sudo systemctl restart microesp-scripts.service`. |
| ModemManager sends `AT` commands to the dongle | The udev rule (`ID_MM_DEVICE_IGNORE`) is missing. |
| Risk-free testing | Start with `microesp-agent run --dry-run --log-level debug`. |

Countdown notices (`notice`) are attempted to be broadcast with `wall`, but the systemd sandbox normally prevents it. That is fine: the countdown is visible on the dongle screen and in the app.

## Development

- `internal/proto`: messages, framing (512 B per line) and HMAC/HKDF. The tests use the normative vectors in `../protocol/testdata/vectors.json`.
- `internal/link`: discovery, session, heartbeat, reconnection and command rules. The tests use `net.Pipe` and a simulated dongle (`internal/link/dongletest`).
- `internal/telemetry`: sampling with gopsutil and the `Collector` interface (includes a fake implementation).
- `internal/power`: `Executor` interface, with systemd, logind-dbus, Windows and DryRun backends. The tests never run a real action.
- `internal/scripts`: user script runner (one instance per id, timeout, process-group kill, output tail), the runner socket server (Linux, `SO_PEERCRED`, socket activation) and its client. The tests run harmless shell commands.
- `internal/config`: TOML, environment and key file.
