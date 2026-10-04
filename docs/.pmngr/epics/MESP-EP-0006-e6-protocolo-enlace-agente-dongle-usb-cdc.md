---
id: MESP-EP-0006
type: epic
title: E6 · Protocolo enlace agente ↔ dongle (USB CDC)
status: done
priority: high
milestone: MESP-M-0003
author: mcp
labels: [protocol, firmware, agent]
created: 2026-10-03T08:57:35Z
updated: 2026-10-04T22:00:42Z
closed: 2026-10-04T22:00:42Z
---

## Description
Especificar e implementar en ambos extremos el protocolo JSON-lines sobre CDC: hello/welcome, telemetría, comandos con ack, heartbeat, autenticación HMAC y versionado. Hace que agente y dongle se vean como un único dispositivo Tuya (ADR-1).

## Acceptance Criteria
- Spec versionada en docs con requisitos trazables.
- Vectores de prueba compartidos pasan en firmware (host test) y Go.
