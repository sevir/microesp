# Spike: power-on from S5 on the Lenovo ThinkStation P3 Ultra SFF G2

Story: MESP-US-0002. Machine and BIOS setup: [`docs/user/bios-lenovo.md`](../user/bios-lenovo.md). Test cases: [`docs/qa/e2e-v1.md`](../qa/e2e-v1.md) (E2E-13/14/15).

Status: **in progress**. First evidence comes from the dongle `!log` ring (firmware 0.4.0, release build) after two real shutdowns on 2026-10-05; the controlled tests are still pending.

## Setup observed

- The dongle sits in the only rear USB port that keeps 5 V in S5 (owner's observation: the dongle stays powered with the PC off).
- Wired NIC `enp128s31f6` (`fc:9d:05:18:ee:32`) is up; NetworkManager `802-3-ethernet.wake-on-lan = default`. The driver WOL mode (`ethtool ... | grep Wake-on`, needs root) is not checked yet.
- `!status` with the PC on: `hid_proto=report`, `rwu_armed=0`. No `HID SET_PROTOCOL` line appears in the log at all, so the S5 host never selected the boot protocol.

## Timeline from the dongle log (dongle clock = uptime; offset to local time +14:50:35)

| Local time | Dongle log | Meaning |
|---|---|---|
| 16:43:12 | `ack id=1 ok=1`, `cdc: port closed`, suspend, **mount** 13 s later | Agent shutdown; in S5 the host re-enumerates the dongle and keeps the bus **mounted and not suspended** |
| 16:43–18:15 | `pc_state -> booting` → `on_no_agent` (90 s grace) | **False positive**: the PC is off but the dongle reports it on without agent |
| 18:15:03 | power_on from cloud: Alt+P `-1`, WOL ×3 MACs, suspend/resume storm, forced resume ×2, mount at +12 s, Alt+P `0` ×2, `PC is up` | PC boots (kernel 18:15:29). The trigger was the first WOL or the forced resume: the mount at +12 s is already POST |
| 18:24:13 | `ack id=1 ok=1`, suspend, **mount** at +7 s | Second shutdown (scheduled from the panel), same S5 pattern |
| 18:31, 18:43 | power_off from cloud → `agent_offline` | Owner thought the PC was hung (dark screen, panel "on, no agent") |
| 18:43:30 | power_on: Alt+P `-1` (`HID key release not sent (bus went away?)`), WOL ×3, suspend/resume storm, forced resume ×2, WOL repeat at +20 s, mount, Alt+P `-1` again | **Power-on failed** (`wake: FAILED`, fault `wake_failed`) |
| 20:57:47 | mount, no wake request | Owner pressed the power button; kernel at 20:58:17 |

The journal confirms both shutdowns completed (`Reached target poweroff.target` → `Journal stopped`): the PC did not hang, it was in S5.

## Findings

1. **S5 looks like "PC on" to the dongle.** With power kept on the port, the S5 host re-enumerates the dongle a few seconds after the shutdown and leaves the bus mounted and not suspended for hours. The `MHAL_USB_MOUNT` event clears `shutdown_expected` (`src/usb_composite.c`), so `pc_state` goes `booting` → `on_no_agent`. The existing `shutdown_expected` rule only covers "mounted and suspended".
2. **The S5 host suspends/resumes the bus as soon as the dongle sends a report.** The Alt+P press is queued but the release fails because the bus suspends within ~30 ms, and then dozens of suspend/resume events follow. Whether the press reached the embedded controller is unknown.
3. **Power-on is not reliable yet.** The same sequence (WOL + forced resume + Alt+P) booted the PC at 18:15 and failed at 18:43. WOL from S5 and HID from S5 have to be tested separately (`wake_method=wol`, then `hid`).

## Next steps

- [x] `sudo ethtool enp128s31f6 | grep Wake-on` shows `Wake-on: g` (supports `pumbg`), checked with the PC on.
- [ ] E2E-14 (WOL only) and E2E-13 (HID only), reading `!log` after each attempt.
- [x] Alt+P with the PC on reaches the foreground application (E2E-16, pass): the HID path works under the OS; the open question is only the S5 host.
- [ ] Firmware: keep `shutdown_expected` across the S5 re-enumeration; see the fix plan.
