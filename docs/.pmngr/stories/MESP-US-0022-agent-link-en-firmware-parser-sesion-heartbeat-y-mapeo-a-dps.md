---
id: MESP-US-0022
type: story
title: "agent_link en firmware: parser, sesión, heartbeat y mapeo a DPs"
status: done
priority: high
parent: MESP-EP-0006
milestone: MESP-M-0003
author: mcp
labels: [protocol, firmware]
estimate: 5
created: 2026-10-03T08:59:05Z
updated: 2026-10-04T00:09:04Z
started: 2026-10-03T10:59:23Z
closed: 2026-10-04T00:09:04Z
---

## Description
Lectura CDC no bloqueante, parser JSON (cJSON) con límites, sesión hello/welcome con verificación de token, mapeo tele→DPs 106/107/108/111/112, MAC→NVS para WOL, envío de cmd y espera de ack, heartbeat y timeout.

## Acceptance Criteria
- Fuzz/tests host con mensajes truncados, gigantes y malformados sin crash.
- Telemetría visible en app ≤30 s tras arrancar el agente.
