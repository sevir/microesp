---
id: MESP-EP-0001
type: epic
title: E1 · Spikes de riesgo y bring-up de la placa
status: backlog
priority: critical
milestone: MESP-M-0001
author: mcp
labels: [spike, hardware, firmware]
created: 2026-10-03T08:57:35Z
updated: 2026-10-03T10:03:02Z
---

## Description
Validar antes de construir los supuestos de mayor riesgo: pinout real del clon Pocket-Dongle-S3 (T-Dongle-S3), wake HID desde S3/S5 en el Lenovo e integración TinyUSB dentro de TuyaOpen. Ver [[00-analisis-arquitectura]] §9.

## Acceptance Criteria
- Cada spike cierra con informe y decisión go/no-go.
- Pinout confirmado en hw/pinout.md.

## Notes
Backup de fábrica en hw/factory-backup/ (sha256 7eb9b4211c55…). Medición de VBUS descartada del alcance (2026-10-03).
