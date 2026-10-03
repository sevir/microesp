---
id: MESP-US-0012
type: story
title: "Botón BOOT: cancelar cuenta atrás, wake local y reset de fábrica"
status: in_review
priority: medium
parent: MESP-EP-0003
milestone: MESP-M-0002
author: mcp
labels: [firmware]
estimate: 2
created: 2026-10-03T08:58:23Z
updated: 2026-10-03T11:57:34Z
started: 2026-10-03T11:38:27Z
---

## Description
GPIO0 con debounce: pulsación corta = cancelar apagado/reinicio en curso o cambiar pantalla; doble pulsación = encender PC; pulsación 10 s = borrar emparejado (reset Tuya).

## Acceptance Criteria
- Cancelación reporta last_result=cancelled y notifica al agente.
- Reset de fábrica deja el dispositivo en modo emparejado.
