---
created_at: 2026-10-05T20:57:33.413547525Z
updated_at: 2026-10-05T20:57:33.413547525Z
tags:
    - operations
    - scripts
    - tooling
    - how-to
---
# Wrapper scripts for the live machine (`scripts/`)

Day-to-day operations on a PC that runs `microesp-agent.service` with the dongle plugged in (production: `lenovop3`). They run from **any directory** (absolute paths) and handle the two usual traps: the agent opens the CDC port exclusively, and shells opened before joining `dialout` cannot open it.

Live deployment rules still apply ([`AGENTS.md`](../../AGENTS.md), "Live deployment: ask first"): these scripts stop and start the agent service, flash the dongle or reinstall the agent, so coding agents must ask the owner before running them.

## Scripts

| Script | What it does | sudo |
|---|---|---|
| `scripts/dongle-flash.sh` | Stops the agent, runs `firmware/tools/flash.sh --app` (app image + `otadata`; **keeps NVS**: Wi-Fi, TuyaLink credentials, agent key), waits up to 30 s for the CDC port, starts the agent | yes (agent stop/start) |
| `scripts/dongle-flash.sh --full` | Same with a full flash: bootloader, partition table, `otadata`, app, `srmodels.bin` (NVS is not in the image list, so it is kept too) | yes |
| `scripts/dongle-cli.sh [cmd...]` | Stops the agent, runs `firmware/tools/mesp_cdc.py` with the IDF environment's Python (ships pyserial), starts the agent. Default command `!status` | yes |
| `scripts/agent-reinstall.sh` | `make -C agent test`, builds `agent/microesp-agent` stamped `v<CONFIG_PROJECT_VERSION>` (from `firmware/app_default.config`), runs `sudo agent/deploy/install.sh --binary ...` (keeps `/etc/microesp`: `agent.toml`, key; keeps the scripts runner and its user; restarts the service), prints the service status | yes (install) |
| `scripts/setup-serial-access.sh` | One-off: adds the user to `dialout` and installs the Espressif udev rule | yes |

`dongle-flash.sh` flashes whatever is in `firmware/dist/microesp_<CONFIG_PROJECT_VERSION>/`: build first (`cd firmware && ./build.sh`; after changing `app_default.config`, e.g. a version bump, `./build.sh clean`, or the incremental build keeps the old version and no new `dist/` folder appears).

## Examples

```bash
scripts/dongle-cli.sh                       # !status
scripts/dongle-cli.sh '!status' '!dp'       # several commands in one session
scripts/dongle-cli.sh '!log' > dongle.log   # 64 KB RAM log ring (lost on dongle reset)
scripts/dongle-flash.sh                     # after ./build.sh
scripts/agent-reinstall.sh
```

Release firmware only accepts the read-only CLI commands (`!help !status !version !dp !log !dfu !usj !reboot !cancel`); `!wake`, `!key`, `!cmd`... need a `./build.sh dev` image.

## How they work (`scripts/lib/dongle-common.sh`)

- `with_dialout CMD...`: runs the command directly if the current shell has the `dialout` group, otherwise through `sg dialout -c` with the arguments quoted (`printf %q`).
- `agent_release`: if `microesp-agent.service` is active, stops it with sudo and registers an `EXIT` trap; `agent_restore` starts it again **whatever the outcome** (flash error, Ctrl+C). If the agent was already stopped, it is left stopped.
- `wait_cdc`: waits for `/dev/serial/by-id/usb-MicroESP_MicroESP_MESP-*-if01` after the dongle resets.

While the agent is stopped the panel shows the agent offline and shutdown/reboot/scripts from the app are refused (`agent_offline`).

## Reading the dongle log

`!log` lines carry the dongle uptime as the clock (`[01-01 03:24:28 ...]`), not the wall time. To map them to local time, take an event visible on both sides (for example `agent: ack id=1` and the agent journal line `command accepted`) and apply the offset; [`analysis/spike-wake.md`](../analysis/spike-wake.md) has a worked timeline.

Related: [[changes/2026-10-05-live-wrapper-scripts.md]], [[dev-setup.md]].
