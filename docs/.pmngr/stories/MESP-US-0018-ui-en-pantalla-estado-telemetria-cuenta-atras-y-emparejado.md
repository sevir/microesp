---
id: MESP-US-0018
type: story
title: "UI en pantalla: estado, telemetría, cuenta atrás y emparejado"
status: done
priority: medium
parent: MESP-EP-0005
milestone: MESP-M-0003
author: mcp
labels: [firmware, ui]
estimate: 5
created: 2026-10-03T08:59:05Z
updated: 2026-10-04T00:09:04Z
started: 2026-10-03T11:57:34Z
closed: 2026-10-04T00:09:04Z
---

## Description
Pantallas 160x80: (1) principal: icono estado PC, hostname, iconos Wi-Fi/nube/agente; (2) telemetría: barras CPU/MEM/DISK libre; (3) cuenta atrás grande con "Pulsa para cancelar"; (4) emparejado BLE / error. Rotación automática cada 5 s o con botón.

## Acceptance Criteria
- Legible a 50 cm; fuentes y layout en docs/ui.md con capturas.
- Valores actualizados ≤2 s tras llegar al firmware.
