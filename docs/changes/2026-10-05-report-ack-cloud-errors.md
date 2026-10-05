---
created_at: 2026-10-05T21:23:28.173553352Z
updated_at: 2026-10-05T21:23:28.173553352Z
tags:
    - changes
    - firmware
    - tuyalink
    - tuya-quirk
---
# 2026-10-05 — Firmware: request property/report_response (sys.ack=1)

What changed:
- `firmware/src/core/tylink.c` `tyl_build_report`: adds `"sys":{"ack":1}` to every `property/report`.
- `firmware/src/cloud.c` `TYL_T_REPORT_RESP`: non-zero code logged with a hint (2003 not defined, 2006 partial failure) and the first 200 bytes of the payload; `!status` prints `report_resp` and `last_code`.
- `firmware/test/host/test_tylink.c`: asserts `sys.ack == 1`.
- Docs: firmware README (topics paragraph, DPs 115/116 note: publish the thing model; limitations), CHANGELOG `Unreleased`.
- `scripts/*.sh`: shellcheck fixes for CI (`source=/dev/null`, SC2034 on `FW`, unused loop var), same invocation as CI passes with shellcheck 0.11.0.

Why: `!status` said `cloud_report_errors=0` while Online Debugging showed `code 2006 partialFailure: properties scripts/script_run not definition`. Per Tuya docs ([[tuya-device-model]] https://developer.tuya.com/en/docs/iot/device_model?id=Kbt4gcmizz8f4) the cloud only answers a report when `sys.ack=1`; the firmware never set it, so the counter could never move. Second finding: TuyaLink thing model edits are a draft until the model is published.

Verification: `./build.sh test` 95/0; release build OK. Not flashed; the real response format in the EU region still has to be seen in `!log` after flashing.

Related: [[changes/2026-10-05-pc-state-s5-false-on.md]], [[operations/live-scripts.md]].
