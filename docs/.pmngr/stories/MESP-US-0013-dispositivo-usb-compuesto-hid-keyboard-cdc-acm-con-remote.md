---
id: MESP-US-0013
type: story
title: Dispositivo USB compuesto HID keyboard + CDC ACM con remote wakeup
status: in_review
priority: critical
parent: MESP-EP-0004
milestone: MESP-M-0002
author: mcp
labels: [firmware, usb]
estimate: 5
created: 2026-10-03T08:59:05Z
updated: 2026-10-03T11:38:27Z
started: 2026-10-03T10:59:23Z
---

## Description
usb_composite con esp_tinyusb: descriptores HID boot keyboard + CDC ACM, bmAttributes con TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, VID/PID y número de serie estables (MAC), strings "MicroESP". Callbacks mount/umount/suspend/resume publicados al bus de eventos. Logs ESP por CDC opcionales (canal separado o prefijo).

## Acceptance Criteria
- Enumera en Linux y Windows como teclado + puerto serie; /dev/serial/by-id/ estable.
- Reset 1200-baud touch sobre CDC entra en modo descarga para flashear.
- Linux muestra power/wakeup=enabled para el dispositivo.
