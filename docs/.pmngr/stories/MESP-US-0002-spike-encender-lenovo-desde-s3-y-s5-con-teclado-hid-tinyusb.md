---
id: MESP-US-0002
type: story
title: "Spike: encender Lenovo desde S3 y S5 con teclado HID TinyUSB"
status: backlog
priority: critical
parent: MESP-EP-0001
milestone: MESP-M-0001
author: mcp
labels: [spike, usb, wake]
estimate: 5
created: 2026-10-03T08:58:23Z
updated: 2026-10-03T08:58:23Z
---

## Description
Como usuario quiero saber si el PC se enciende con el dongle. Firmware ESP-IDF + esp_tinyusb: HID keyboard con bmAttributes REMOTE_WAKEUP; al pulsar BOOT, si tud_suspended() → tud_remote_wakeup(); si no montado, intentar señalización de resume. Basarse en github.com/nonoo/esp-remote-wakeup.

Matriz de prueba en el Lenovo: S3 (suspend), S4 (hibernar), S5 (apagado) × puertos USB (trasero, frontal, Always-On marcado) × BIOS (Wake on keypress / USB wake / Always On USB / Deep sleep off / Fast Startup off).

## Acceptance Criteria
- Tabla de resultados en docs/analisis/spike-wake.md.
- Confirmado si el puerto da 5 V en S5 (multímetro o LED del dongle encendido).
- Decisión: HID suficiente / WOL obligatorio / mod botón power.

## Notes
Riesgo arduino-esp32 #10831 (disconnect vs suspend). Linux: /sys/bus/usb/devices/*/power/wakeup=enabled.
