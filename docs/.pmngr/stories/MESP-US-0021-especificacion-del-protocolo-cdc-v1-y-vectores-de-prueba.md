---
id: MESP-US-0021
type: story
title: Especificación del protocolo CDC v1 y vectores de prueba
status: done
priority: high
parent: MESP-EP-0006
milestone: MESP-M-0003
author: mcp
labels: [protocol]
estimate: 3
created: 2026-10-03T08:59:05Z
updated: 2026-10-03T11:38:27Z
started: 2026-10-03T10:18:52Z
closed: 2026-10-03T11:38:27Z
---

## Description
Spec docs/protocol/cdc-v1.md según [[00-analisis-arquitectura]] §7: framing JSON-lines ≤512 B, mensajes hello/welcome/tele/cmd/ack/ping, versionado, errores, HMAC-SHA256 (id|action|ts, ventana ±60 s, ids no reutilizables), emparejado del token. JSON Schema + vectores en protocol/testdata/.

## Acceptance Criteria
- Spec revisada; schema valida todos los vectores.
- Vectores usados por tests Go y firmware.
