---
created_at: 2026-10-05T20:28:56.01869353Z
updated_at: 2026-10-05T20:28:56.01869353Z
tags:
    - changes
    - firmware
    - pc_state
    - s5
---
# 2026-10-05 — Firmware: pc_state stays off after a remote shutdown (phase 1)

Plan: [[plans/pc-state-s5-false-on-fix_plan.md]]. Evidence: [[analysis/spike-wake.md]].

What changed:
- `firmware/src/core/pc_state.c` `pcs_raw`: with `shutdown_expected` -> `PCS_BOOTING` if a wake is in progress, else `PCS_OFF`, whatever the bus state; `pcs_update` does not raise `agent_lost` while it is set.
- `firmware/src/core/pc_state.h`: rule table and `shutdown_expected` comment.
- `firmware/src/usb_composite.c` `usbc_on_event`: MOUNT/RESUME no longer clear `g_app.shutdown_expected`.
- `firmware/src/app_main.c` `dispatch` (`EV_CDC_DTR`): port opened by the host clears it (agent `ready` already did).
- Tests: `test_shutdown_with_bios_reenumeration`, `test_shutdown_expected_while_agent_online` in `firmware/test/host/test_pc_state.c`.
- Docs: firmware README "PC state", CHANGELOG `Unreleased` / Fixed.

Why: the Lenovo S5 host re-enumerates the dongle on its powered port; the mount cleared the flag and the panel showed "on, no agent" with the PC off.

Verification: `./build.sh test` 95 tests, 0 failures (ASan/UBSan); `./build.sh` release build OK. Not flashed yet (needs the owner's OK).
