---
id: MESP-US-0026
type: story
title: "Telemetría: CPU, memoria, % disco libre, uptime, hostname y MAC"
status: done
priority: high
parent: MESP-EP-0007
milestone: MESP-M-0003
author: mcp
labels: [agent, go, telemetry]
estimate: 3
created: 2026-10-03T08:59:43Z
updated: 2026-10-04T00:09:04Z
started: 2026-10-03T10:18:52Z
closed: 2026-10-04T00:09:04Z
---

## Description
internal/telemetry con gopsutil/v4: CPU % (ventana 5 s), memoria usada % (excluyendo cache/buffers), % libre del FS configurado (por defecto `/`, lista configurable → se reporta el mínimo), uptime, hostname, MACs de NICs físicas con WOL. Envío cada 10 s o cambio >2 puntos.

## Acceptance Criteria
- Valores coinciden con top/free/df con tolerancia ±2 puntos.
- Tests con proveedores simulados (interfaz Collector).
