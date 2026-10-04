---
id: MESP-US-0001
type: story
title: "Spike: confirmar pinout del clon (LCD ST7735, backlight, LED, botón, TF)"
status: done
priority: critical
parent: MESP-EP-0001
milestone: MESP-M-0001
author: mcp
labels: [spike, hardware]
estimate: 3
created: 2026-10-03T08:58:23Z
updated: 2026-10-04T00:09:04Z
started: 2026-10-03T10:18:52Z
closed: 2026-10-04T00:09:04Z
---

## Description
Como desarrollador quiero el pinout real del Pocket-Dongle-S3 para no asumir el del LilyGO T-Dongle-S3. Firmware de prueba ESP-IDF mínimo: init ST7735 con pines de referencia (MOSI 3, SCLK 5, CS 4, DC 2, RST 1, BL 38), barrido de colores, LED WS2812 probando GPIO 40/39/48/38, lectura botón GPIO0, montaje TF.

## Acceptance Criteria
- hw/pinout.md con tabla verificada y foto de la placa.
- Offsets de la pantalla ST7735 80x160 (col/row offset, inversión, orden RGB/BGR) documentados.
- LED identificado (WS2812 vs APA102) y pin.

## Notes
Restaurable con hw/factory-backup/.
