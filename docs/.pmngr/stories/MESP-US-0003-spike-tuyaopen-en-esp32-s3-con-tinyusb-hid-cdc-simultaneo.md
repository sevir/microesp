---
id: MESP-US-0003
type: story
title: "Spike: TuyaOpen en ESP32-S3 con TinyUSB HID+CDC simultáneo"
status: done
priority: critical
parent: MESP-EP-0001
milestone: MESP-M-0001
author: mcp
labels: [spike, tuya, usb]
estimate: 5
created: 2026-10-03T08:58:23Z
updated: 2026-10-03T10:59:23Z
started: 2026-10-03T10:18:52Z
closed: 2026-10-03T10:59:23Z
---

## Description
Validar que una app TuyaOpen (tos.py, plataforma ESP32, chip esp32s3) puede incluir el componente esp_tinyusb y exponer HID+CDC mientras mantiene Wi-Fi/BLE y conexión cloud. Usar switch_demo con licencia dev.

## Acceptance Criteria
- Build reproducible documentado (versión TuyaOpen, IDF, comandos).
- Dispositivo empareja por BLE, reporta un DP bool y a la vez enumera como HID+CDC en el PC.
- Uso de RAM/flash y particiones OTA medidos (16 MB flash, 8 MB PSRAM).
- Si no es viable: plan B (proyecto ESP-IDF propio + librerías tuya_cloud_service de TuyaOpen) con estimación.
