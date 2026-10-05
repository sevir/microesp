# AGENTS.md

Guide for coding agents working in this repository. Read it before changing anything; the details live in the linked docs.

## MANDATORY operating rules for AI agents

Every rule in this section is **MANDATORY**, not optional or best-effort. Agents working in this repository must follow them for every task, including small changes. If a shortcut or habit conflicts with a rule, follow the rule. Use the named tools when they are available; if one is unavailable, use the closest equivalent and state that limitation.

### MANDATORY: recover context before acting

- Start with `kb_search_documents` and no `tags` filter to recover relevant project docs, plans, fixes, and prior decisions. `hybrid_search_remembrances` is also suitable when you need one query across KB documents, remembered facts, sessions, and indexed code. Do not begin from a blank slate when relevant context may exist.
- Follow KB wiki links: use `kb_get_document` to inspect a relevant document and its links, then `kb_related_documents` to traverse its neighbours. Use `kb_related_documents` without a path to see undocumented concepts when useful; prefer following an author's links over repeatedly guessing searches.
- Before editing code, use the code-index tools to understand the code and its established patterns: `code_get_symbols_overview`, `code_find_symbol`, `code_hybrid_search`, and `code_search_pattern`. Prefer these to blind file reads when locating code; use `code_find_references` before changing a symbol's callers or contract. If the index has no relevant results, say so briefly and use filesystem tools for exact content or further exploration.
- Use `recall` only when you already know roughly which short, durable fact or key you need; it is not a substitute for the broad KB search.

### MANDATORY: research facts that are not established in the repository

- For third-party library, framework, or API usage, resolve the library first with `c7_resolve_library_id`, then retrieve current, version-appropriate documentation with `c7_get_library_docs`. Prefer those docs over memory or guesswork.
- For general or unfamiliar facts, current events, error messages, or release information not established in the repo/KB, use web search (`google_search`, `brave_search`, or `exa_search` where available) and `fetch` the relevant sources. Cross-check more than one source for load-bearing claims. If these search tools are unavailable, use the closest available research tool and state the limitation.
- For frontend or web-UI work, use the browser tools to load and inspect the actual UI: `browser_navigate`, `browser_get_content`, `browser_evaluate`, `browser_click`, `browser_fill`, `browser_screenshot`, `browser_console_logs`, and `browser_network`. Verify behavior by driving the page, not by inferring it from source alone.

### MANDATORY: plan non-trivial work before implementation

For work larger than a trivial change, write a plan before implementing. Break it into independently verifiable phases and save it using `kb_add_document` at a clear path such as `plans/<short-slug>_plan.md`; update it as phases finish. Search the KB first if a plan may already exist, and confirm with the user before diverging from an existing plan. Do not claim completion for unfinished phases.

### MANDATORY: implement and verify in small increments

- Make small, testable changes and run the relevant tests or build after each meaningful increment before continuing. Match surrounding naming, style, comment density, and idioms; follow this repository's language-specific and project conventions.
- Add or update tests for new behavior, placing them where the project expects them. Use the build/test commands below and the component READMEs; do not assume an unverified test framework or dependency.
- Report work as done only when it has been verified by tests, a successful build, or direct observation. State plainly which checks were skipped or failed and include the evidence.

### MANDATORY: document every completed change in the KB

After every modification, implementation, fix, or refactor—including a small or documentation-only change—record a summary with `kb_add_document`. Use a clear path under `changes/`, `fixes/`, or `features/` (or update the related existing document rather than duplicating it). Include what changed, the concrete files and symbols touched, why it changed, and how it was verified. Link the plan, feature, or fix it builds on with a `[[wiki link]]`; a link to a not-yet-documented concept is acceptable. In plan mode, where edits are restricted to the plan, defer the change-summary write until approved writes are permitted, but do not omit it.

### MANDATORY: choose the right memory store

Use `remember` and `recall` only for short, durable facts identified by a known key (for example `project.test_command` or `user.preferred_lang`); use `remember` to upsert and `recall` when you know roughly what key to retrieve. Do not store long or structured material there. Use `kb_add_document` and `kb_search_documents` for plans, analyses, design notes, ordered decisions, references, and broad pre-task context searches.

### MANDATORY: general conduct and confirmation

Use English for code, comments, and documentation, while preserving existing product UI strings as directed below. Parallelize independent work when useful, but provide delegates self-contained instructions and enough context to preserve correctness. Confirm before hard-to-reverse or outward-facing actions unless there is durable authorization; the live-deployment safeguards below are stricter and must always be followed.

## What this is

MicroESP turns an ESP32-S3 USB dongle (Pocket-Dongle-S3, a clone of the LilyGO T-Dongle-S3) into a remote PC power switch controlled from the Tuya / Smart Life app: power on (USB HID remote wakeup + Alt+P, Wake-on-LAN in parallel), shut down / reboot with a cancellable countdown, user scripts, PC state and telemetry. Personal project of José F. Rives.

Architecture (decisions in [`docs/analysis/00-architecture-analysis.md`](docs/analysis/00-architecture-analysis.md), ADR-1..5):

- **Dongle = the only Tuya cloud client.** Firmware on TuyaOpen v1.9.0 / ESP-IDF v5.4, board `POCKET_DONGLE_S3` (16 MB). Cloud protocol is **TuyaLink** (MQTT over TLS, own client on `esp-mqtt`), not TuyaOS (ADR-5). TuyaOpen is kept only as framework (RTOS `tal_*`, LVGL, build).
- **Composite USB**: HID keyboard (power-on) + CDC ACM (agent link + `!` CLI), VID/PID `303a:4002`.
- **PC agent** `microesp-agent` (Go): talks to the dongle over CDC with the [`cdc-v1`](docs/protocol/cdc-v1.md) protocol (HMAC-SHA256 signed commands, key from HKDF of a 6-digit pairing code), sends telemetry/heartbeat, runs signed shutdown/reboot via logind/polkit and user scripts.
- **Panel** (`panel/`): Ray Panel MiniApp for Smart Life, uses the TuyaLink thing model (`publishThingModelMessage`, ...).
- DP / thing model source of truth: [`firmware/schema/dp.json`](firmware/schema/dp.json) (abilityIds 101–116). It must stay in sync with `firmware/src/core/dp_model.c` (host test) and `panel/src/device/model.ts` (`npm test`).

## Repository layout

| Path | Contents |
|---|---|
| `firmware/` | Production firmware ([README](firmware/README.md)): `src/` app, `src/core/` pure C logic (host-tested), `esp_components/mesp_hal/` ESP-IDF HAL, `board/`, `schema/dp.json`, `test/host/`, `tools/` |
| `agent/` | Go agent ([README](agent/README.md)): `cmd/microesp-agent`, `internal/{proto,link,telemetry,power,scripts,config}`, `deploy/` (install.sh, systemd, udev, polkit, Windows) |
| `panel/` | Ray Panel MiniApp ([README](panel/README.md)); `scripts/ide-wine/` runs the Tuya MiniApp IDE under Wine |
| `panel-design/` | HTML export of the panel design |
| `protocol/testdata/vectors.json` | Normative cdc-v1 vectors used by agent and firmware tests |
| `docs/` | Knowledge base (see below) |
| `hw/` | `pinout.md`, hardware spikes (`spikes/pinout`, `spikes/tuya-usb`, `spikes/tylink_test.py`), factory backup (only `SHA256SUMS` committed) |
| `scripts/` | `setup-serial-access.sh`, `dongle-flash.sh`, `dongle-cli.sh`, `agent-reinstall.sh` (wrappers that stop/restart the live agent), `lib/`, `ci/fetch-host-test-deps.sh` |
| `.github/workflows/` | `ci.yml`, `firmware-build.yml` (reusable), `release.yml` |

## Environment

Tooling lives in `/www/MicroESP/tools` ([`docs/dev-setup.md`](docs/dev-setup.md)): TuyaOpen checkout (`TuyaOpen`, v1.9.0 commit `b80932d`), `tos-env.sh` (TuyaOpen / `tos.py`), `idf-env.sh` (bare ESP-IDF / `idf.py`). Do not mix both environments in one shell. Go ≥ 1.23 (CI uses 1.26), Node ≥ 22.6 for the panel.

Serial port access needs the `dialout` group; in shells opened before joining it, wrap every command that opens the port with `sg dialout -c "..."`. The agent opens the CDC port exclusively: stop it before using the CLI.

## Build, test, flash

Firmware ([`firmware/README.md`](firmware/README.md)):

```bash
cd firmware
./build.sh             # incremental RELEASE build (MESP_DEV_CLI=n)
./build.sh dev         # DEVELOPMENT build (full CLI, DEBUG log)
./build.sh clean [dev] # after changing sdkconfig.microesp, the board or app_default.config
./build.sh test        # host Unity tests with ASan/UBSan (no ESP toolchain), = make -C test/host
# output: dist/microesp_<ver>/...
sg dialout -c "tools/flash.sh"        # full flash, no BOOT button needed (1200-baud touch)
sg dialout -c "tools/flash.sh --app"  # otadata + app only
tools/mesp_cdc.py '!status' '!dp'     # CDC CLI (sg dialout + IDF env Python)
# From any directory, on the live machine (stop the agent, run, start it again; sudo):
scripts/dongle-flash.sh [--full]       # flash the last build (default: app + otadata)
scripts/dongle-cli.sh '!status' '!log' # CDC CLI
scripts/agent-reinstall.sh             # test, build (version from app_default.config) and install the agent
```

Never use esptool `--before no_reset`. Memory: `python -m esp_idf_size dist/microesp_<ver>/microesp_<ver>.map` (not `idf.py size`).

Agent ([`agent/README.md`](agent/README.md)):

```bash
make -C agent lint test cover   # vet (linux+windows), staticcheck, gofmt, shellcheck, tests -race, coverage >= 70 %
make -C agent build             # ./agent/microesp-agent
make -C agent cross             # dist/: linux-amd64, linux-arm64, windows-amd64.exe
./agent/deploy/install.sh --dry-run
microesp-agent run --dry-run --log-level debug   # risk-free local run
```

Panel ([`panel/README.md`](panel/README.md), publishing: [`docs/panel-publishing.md`](docs/panel-publishing.md)):

```bash
cd panel && npm install
npm test             # model.ts against firmware/schema/dp.json
npm run typecheck
npm run build:tuya   # dist/tuya
```

CI (`.github/workflows/ci.yml`, on push to main / PR): `agent`, `firmware-host-tests`, `firmware-build` (TuyaOpen pinned to `b80932d`, placeholder credentials, esp-sr pinned `==2.4.7`), `secrets` (gitleaks, `.gitleaks.toml`), `shell` (shellcheck on every `*.sh`). Run the local equivalents before proposing a commit.

## Versioning and release

- Firmware and agent share one SemVer version. Single source: `CONFIG_PROJECT_VERSION` in `firmware/app_default.config`. The panel's `panel/package.json` version follows it.
- `CHANGELOG.md` follows Keep a Changelog: add a `## X.Y.Z - YYYY-MM-DD` section (Added / Changed / Fixed / Removed).
- A `vX.Y.Z` tag triggers `release.yml`, which **fails if the tag does not match `CONFIG_PROJECT_VERSION`**; it publishes the agent (goreleaser) and the firmware (merged image + app) with a common `SHA256SUMS`.
- Release commits look like `chore(release): X.Y.Z` (CHANGELOG + `app_default.config`).

## Rules

- **English everywhere**: documentation, code comments, commit messages, CHANGELOG. Product UI strings (dongle screen, agent CLI output, panel `strings/`) stay as they are.
- **Conventional commits** with a scope when it fits: `feat(firmware): ...`, `fix(panel): ...`, `docs(qa): ...`, `ci(agent): ...`, `chore(release): ...`.
- **Never commit secrets**: TuyaLink deviceSecret, Wi-Fi password, agent key (`*.key`, `/etc/microesp/agent.key`), `*.pem`, `.env`, `tuya_secrets.h` / `tuya_config_secrets.h`, factory flash backups (`hw/factory-backup/*.bin`). Do not print the full deviceId in docs or logs (use the masked form). Secrets reach the dongle only through the CDC CLI (`!tylink <region> <productId> <deviceId> <deviceSecret>`, `!wifi <ssid> <password>`), stored in NVS. The Wine IDE log (`~/.wine-tuya/wine-run.log`) can contain session tokens.
- Keep `firmware/schema/dp.json`, `dp_model.c`, `panel/src/device/model.ts` and the docs in sync when touching DPs. New DPs must first be created on the Tuya platform (see firmware README, "DPs").
- `firmware/src/core/` must stay free of RTOS/IDF dependencies (host-tested). IDF-specific code goes in `esp_components/mesp_hal` behind `include/mesp_hal.h`.
- cdc-v1 changes: update `docs/protocol/cdc-v1.md` and, if relevant, `protocol/testdata/vectors.json`; both agent and firmware tests consume them.

## Knowledge base

`docs/` is the knowledge base, indexed by Pando (`.pando.toml`, local and git-ignored: `KBPath = /www/MicroESP/microesp/docs`, `KBWatch = true`). Current sections: `analysis/` (architecture, ADRs, `spike-wake.md`), `operations/` (live-machine scripts), `protocol/`, `user/` (installation, BIOS), `qa/` (E2E plan and results), `dev-setup.md`, `panel-publishing.md`. `docs/.pmngr/` holds the backlog (epics/stories/milestones, ids `MESP-*`): never renumber or reuse an id.

When you learn something worth keeping (a decision, a hardware finding, a troubleshooting recipe, a Tuya platform quirk), write it as English Markdown in the right `docs/` subfolder (or extend the existing page) and link it from the relevant README. For every change, also follow the MANDATORY KB change-record rule above and register its summary with `kb_add_document`; use `remember` only for short, durable keyed facts.

## Live deployment: ask first

MicroESP is in production on José's own PC, hostname `lenovop3` (Lenovo ThinkStation P3 Ultra SFF G2, Pop!_OS), with `microesp-agent.service` and `microesp-scripts.socket` active and the dongle plugged in. **Confirm with José before** any of:

- touching the installed service or its config (`systemctl`, `install.sh` without `--dry-run`, `/etc/microesp/*`, `microesp-agent pair`);
- flashing or rebooting the physical dongle, using its CDC CLI, or anything that reads/writes NVS;
- changes on the Tuya platform (product, DPs, device, panel/MiniApp upload or release);
- any real power action (shutdown, reboot, power-on, scripts). Use `dry_run` / `--dry-run` for tests.

Also do not connect another client with the dongle's TuyaLink credentials (e.g. `hw/spikes/tylink_test.py`): only one connection per deviceId is allowed and it kicks the dongle off the cloud.
