---
id: MESP-EP-0003
type: epic
title: E3 · Firmware base TuyaOpen ESP32-S3
status: done
priority: high
milestone: MESP-M-0002
author: mcp
labels: [firmware, tuya]
created: 2026-10-03T08:57:35Z
updated: 2026-10-04T22:00:42Z
closed: 2026-10-04T22:00:42Z
---

## Description
Proyecto firmware en firmware/ con TuyaOpen (tos.py) para board pocket-dongle-s3: arranque, emparejado BLE, conexión cloud, despacho de DPs, persistencia NVS, OTA y botón.

## Acceptance Criteria
- Emparejado BLE en Smart Life y reconexión tras reinicio/pérdida Wi-Fi.
- OTA desde la plataforma Tuya aplicada con rollback seguro.
