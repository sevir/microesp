---
id: MESP-US-0014
type: story
title: Encender PC desde la app (DP 102) con HID y fallback Wake-on-LAN
status: in_review
priority: critical
parent: MESP-EP-0004
milestone: MESP-M-0002
author: mcp
labels: [firmware, wake]
estimate: 5
created: 2026-10-03T08:59:05Z
updated: 2026-10-03T11:38:27Z
started: 2026-10-03T10:59:23Z
---

## Description
Como usuario quiero encender el PC desde Tuya. Módulo wake: según wake_method (DP 110) → hid: si suspendido tud_remote_wakeup(); si no montado, secuencia de resume/keypress según resultado del spike; wol: magic packet UDP 9 broadcast con MAC aprendida del hello del agente (persistida en NVS); hid_then_wol: HID y si en 20 s no hay mount → WOL.

## Acceptance Criteria
- Desde suspendido: PC despierta en ≤5 s.
- Desde apagado: funciona por el método validado en el spike.
- last_result (DP 114) = wake_sent / wake_failed; fault bit wake_failed si no hay mount en 120 s.
- Ignorar power_on si el PC ya está on.
