---
id: MESP-US-0009
type: story
title: "Esqueleto firmware: board pocket-dongle-s3 y estructura de módulos"
status: in_review
priority: high
parent: MESP-EP-0003
milestone: MESP-M-0002
author: mcp
labels: [firmware]
estimate: 3
created: 2026-10-03T08:58:23Z
updated: 2026-10-03T11:38:27Z
started: 2026-10-03T10:59:23Z
---

## Description
Crear firmware/ como app TuyaOpen con board config propio (pinout de hw/pinout.md, 16 MB flash, PSRAM octal/quad según chip), tabla de particiones con OTA dual y módulos vacíos: app_main, tuya_dp, usb_composite, wake, display, vbus, agent_link, state.

## Acceptance Criteria
- `tos.py build` limpio; flasheable y con log de arranque.
- Bus de eventos interno (esp_event o cola FreeRTOS) entre módulos.
