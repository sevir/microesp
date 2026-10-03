---
id: MESP-US-0015
type: story
title: Máquina de estados del PC (DP 101) y agent_online (DP 109)
status: in_review
priority: high
parent: MESP-EP-0004
milestone: MESP-M-0002
author: mcp
labels: [firmware, state]
estimate: 3
created: 2026-10-03T08:59:05Z
updated: 2026-10-03T11:38:27Z
started: 2026-10-03T11:38:27Z
---

## Description
Módulo state: fusiona eventos USB (mount/suspend/resume/umount), heartbeat del agente (timeout 15 s) y wake en curso → off, sleep, booting, on_no_agent, on, unknown. Transiciones con histéresis para evitar rebotes en el arranque.

## Acceptance Criteria
- Tabla de transiciones documentada y cubierta por tests host (Unity/ceedling).
- Cambio de estado reportado a Tuya y pantalla en ≤10 s.
