---
id: MESP-US-0025
type: story
title: "Enlace serie con el dongle: descubrimiento, sesión y reconexión"
status: done
priority: high
parent: MESP-EP-0007
milestone: MESP-M-0003
author: mcp
labels: [agent, go, protocol]
estimate: 5
created: 2026-10-03T08:59:43Z
updated: 2026-10-04T00:09:04Z
started: 2026-10-03T10:18:52Z
closed: 2026-10-04T00:09:04Z
---

## Description
internal/link: descubrir por VID/PID + serial (go.bug.st/serial/enumerator, /dev/serial/by-id), abrir CDC, hello/welcome con token, lector/escritor JSON-lines, heartbeat, backoff exponencial ante desconexión, detección de dongle suplantado (HMAC de welcome).

## Acceptance Criteria
- Desenchufar/enchufar el dongle → sesión restablecida ≤10 s.
- Tests con puerto simulado (pipe) usando vectores de protocol/testdata.
