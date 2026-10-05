---
created_at: 2026-10-05T20:14:20.991692326Z
updated_at: 2026-10-05T20:14:20.991692326Z
tags:
    - changes
    - wake
    - s5
    - agent
---
# 2026-10-05 — S5 shutdown/power-on evidence and agent rebuild

What changed:
- New `docs/analysis/spike-wake.md` ([[analysis/spike-wake.md]]): timeline reconstructed from the dongle `!log` ring of two real shutdowns (16:43, 18:24) and two power-on attempts (18:15 ok, 18:43 failed). Linked from `docs/user/bios-lenovo.md` (validation status note).
- Agent rebuilt from `main` as `v0.4.0` (agent code identical to tag v0.4.0; only a test changed since). The installed binary was `v0.3.0-dirty` (built 13:05 from a work tree). Reinstall pending: needs `sudo agent/deploy/install.sh --binary agent/microesp-agent` by the owner.

Why: owner saw the PC "hung" after a scheduled shutdown and missing panel buttons. Findings: agent, dongle and cloud work (`cloud_report_errors=0`, DP 115 published); the PC was really in S5; the dongle reported `on_no_agent` because the S5 host re-enumerates the dongle and MOUNT clears `shutdown_expected`; power-on from S5 is unreliable (Alt+P release fails as the bus suspends; WOL untested alone). Panel buttons: the Smart Life panel is likely an older MiniApp version.

Verification: `make -C agent test` passed; `install.sh --dry-run` reviewed; read-only `!status`, `!dp`, `!log` over CDC with the agent stopped by the owner.

Related: [[MESP-US-0002]], [[pc-state-s5-false-on-fix]].
