---
id: MESP-US-0004
type: story
title: "Spike: diseño del mod de medida VBUS (divisor a ADC1)"
status: cancelled
priority: high
parent: MESP-EP-0001
milestone: MESP-M-0001
author: mcp
labels: [spike, hardware, vbus]
estimate: 3
created: 2026-10-03T08:58:23Z
updated: 2026-10-03T10:03:02Z
closed: 2026-10-03T10:03:02Z
---

## Description
La placa no mide VBUS. Localizar pad VBUS (conector USB-A / entrada LDO) y GPIO ADC1 libre accesible (pads GPIO6-10 o pin de la ranura TF). Diseñar divisor 200k/100k (+100 nF a GND) y validar lectura con adc_oneshot + calibración.

## Acceptance Criteria
- hw/vbus-mod.md con esquema, fotos y GPIO elegido.
- Lecturas comparadas con multímetro a 4,5 / 5,0 / 5,25 V: error ≤±0,1 V.
- Decisión de si el mod se hace o se entrega sin VBUS (DP 105 "no disponible").
