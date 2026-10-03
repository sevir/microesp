---
id: MESP-US-0017
type: story
title: Driver ST7735 80x160 + LVGL v9 y backlight
status: in_review
priority: medium
parent: MESP-EP-0005
milestone: MESP-M-0002
author: mcp
labels: [firmware, ui]
estimate: 3
created: 2026-10-03T08:59:05Z
updated: 2026-10-03T11:57:34Z
started: 2026-10-03T11:57:34Z
---

## Description
esp_lcd (panel ST7735 o st7789 compatible con offsets) sobre SPI con DMA, integración LVGL v9 de TuyaOpen, buffers parciales en PSRAM/SRAM, control de backlight PWM (activo bajo) con atenuación nocturna.

## Acceptance Criteria
- 30 fps en animación de prueba sin tearing visible.
- Colores y orientación correctos (horizontal 160x80).
