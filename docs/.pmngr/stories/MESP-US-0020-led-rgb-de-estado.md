---
id: MESP-US-0020
type: story
title: LED RGB de estado
status: in_review
priority: low
parent: MESP-EP-0005
milestone: MESP-M-0002
author: mcp
labels: [firmware, ui]
estimate: 1
created: 2026-10-03T08:59:05Z
updated: 2026-10-03T11:57:34Z
started: 2026-10-03T11:57:34Z
---

## Description
LED (WS2812 vía RMT o APA102 según spike): azul parpadeo = emparejando, verde = PC on con agente, ámbar = on sin agente, tenue = PC off, rojo = error, blanco pulso = wake enviado. Brillo bajo por defecto.

## Acceptance Criteria
- Mapa de colores documentado en docs/ui.md.
