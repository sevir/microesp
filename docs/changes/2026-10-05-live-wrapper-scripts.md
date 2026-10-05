---
created_at: 2026-10-05T20:53:22.521013332Z
updated_at: 2026-10-05T20:53:22.521013332Z
tags:
    - changes
    - scripts
    - tooling
---
# 2026-10-05 — Wrapper scripts for the live machine

What changed:
- `scripts/lib/dongle-common.sh`: `with_dialout` (runs directly or via `sg dialout`, absolute paths so it works from any directory), `agent_release`/`agent_restore` (stop `microesp-agent.service` and restart it on EXIT via trap), `wait_cdc`.
- `scripts/dongle-flash.sh [--app|--full]`: stops the agent, runs `firmware/tools/flash.sh`, waits for the CDC port, restarts the agent.
- `scripts/dongle-cli.sh [cmds...]`: stops the agent, runs `firmware/tools/mesp_cdc.py` with the IDF Python (pyserial), restarts it. Default `!status`.
- `scripts/agent-reinstall.sh`: `make -C agent test`, build stamped with `CONFIG_PROJECT_VERSION`, `sudo install.sh --binary`.
- Docs: AGENTS.md (layout table, build/flash block), firmware README (CLI tool paragraph).

Why: the owner ran `sg dialout -c "tools/flash.sh --app"` from the repo root and got `not found`; the manual stop/flash/start sequence was error-prone.

Verification: `bash -n`; usage error path (`--bad` -> rc 2); `with_dialout` smoke test (ls of /dev/ttyACM0, IDF Python imports pyserial 3.5). shellcheck not installed locally (CI `shell` job will run it). Not run end to end (needs sudo and touches the live agent).

Related: [[changes/2026-10-05-pc-state-s5-false-on.md]].
