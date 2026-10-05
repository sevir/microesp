---
created_at: 2026-10-05T20:27:46.502747275Z
updated_at: 2026-10-05T20:29:04.060110005Z
tags:
    - plan
    - firmware
    - pc_state
    - s5
---
# Plan: pc_state false "on" in S5 (pc-state-s5-false-on-fix)

Evidence: [[analysis/spike-wake.md]], [[changes/2026-10-05-s5-wake-evidence.md]]. After an acked shutdown the Lenovo S5 host re-enumerates the dongle (mount, bus not suspended for hours); `MHAL_USB_MOUNT` cleared `shutdown_expected`, so `pc_state` went `booting` -> `on_no_agent` and fault `agent_lost` was raised while the PC was off.

## Phase 1 — firmware
1. `src/usb_composite.c`: MOUNT/RESUME no longer clear `shutdown_expected`.
2. Clear it only when an OS is proven back: agent session `ready` (already) or the CDC port opened by the host (`EV_CDC_DTR` open, `src/app_main.c`).
3. `src/core/pc_state.c`: with `shutdown_expected`, no agent -> `OFF` (or `BOOTING` while a wake runs); no `agent_lost`.
4. Host tests, `pc_state.h`, firmware README, CHANGELOG `Unreleased`.
Result: code done, 95 host tests pass, release build OK ([[changes/2026-10-05-pc-state-s5-false-on.md]]). Pending: version bump, flash with the owner's OK, real check (remote shutdown -> panel shows `off`).

Known limits: a manual power-button boot after a remote shutdown shows `off` until the agent opens the port; a poweroff that fails while the agent stays connected keeps the flag until the next session.

## Phase 2 — agent notice on OS-initiated shutdown (later)
Agent listens to logind `PrepareForShutdown` and tells the dongle (cdc-v1 change) so a desktop shutdown is also reported as `off`.

## Status
- [x] Phase 1 code + tests (flash and field check pending)
- [ ] Phase 2
