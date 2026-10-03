---
id: MESP-US-0019
type: story
title: Medida de VBUS con ADC calibrado y reporte DP 105
status: cancelled
priority: medium
parent: MESP-EP-0005
milestone: MESP-M-0002
author: mcp
labels: [firmware, vbus, hardware]
estimate: 3
created: 2026-10-03T08:59:05Z
updated: 2026-10-03T10:03:02Z
closed: 2026-10-03T10:03:02Z
---

## Description
Módulo vbus: adc_oneshot ADC1 en GPIO del mod, calibración curve-fitting, media móvil de 16 muestras a 10 Hz, factor divisor configurable en Kconfig. Reporte DP 105 si cambia ≥0,05 V o cada 60 s; fault vbus_low si <4,5 V. Build sin mod: DP marcado no disponible.

## Acceptance Criteria
- Error ≤±0,1 V frente a multímetro en 4,5-5,25 V.
- Kconfig MESP_VBUS_ENABLE desactiva limpiamente el módulo.
