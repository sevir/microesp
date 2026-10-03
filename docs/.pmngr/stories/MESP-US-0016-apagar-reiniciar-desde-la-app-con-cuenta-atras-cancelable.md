---
id: MESP-US-0016
type: story
title: Apagar/reiniciar desde la app con cuenta atrás cancelable (DP 103/104/113)
status: in_review
priority: high
parent: MESP-EP-0004
milestone: MESP-M-0003
author: mcp
labels: [firmware, safety]
estimate: 3
created: 2026-10-03T08:59:05Z
updated: 2026-10-03T11:38:27Z
started: 2026-10-03T11:38:27Z
---

## Description
Al recibir power_off/reboot: si agent_online, iniciar cuenta atrás cmd_countdown (def. 10 s) en pantalla, cancelable por botón o app; al expirar enviar cmd firmado al agente y esperar ack. Si agente offline → last_result=agent_offline.

## Acceptance Criteria
- Ack ok → last_result=ok; timeout 10 s sin ack → cmd_rejected.
- DP 103/104 vuelven a false tras ejecutar.
- Cancelación en cualquier momento antes del envío.
