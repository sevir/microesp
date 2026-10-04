---
id: MESP-EP-0007
type: epic
title: E7 · Agente Go microesp-agent
status: done
priority: high
milestone: MESP-M-0003
author: mcp
labels: [agent, go]
created: 2026-10-03T08:57:35Z
updated: 2026-10-04T22:00:42Z
closed: 2026-10-04T22:00:42Z
---

## Description
Binario Go que descubre el dongle por USB, envía telemetría (CPU, memoria, % disco libre, uptime, hostname, MAC) y ejecuta apagado/reinicio recibidos desde Tuya a través del dongle.

## Acceptance Criteria
- Reconexión automática ante desenchufe/reinicio del dongle.
- Consumo <1% CPU y <30 MB RSS en reposo.
- Cobertura de tests ≥70% en internal/.
