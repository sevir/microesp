---
id: MESP-EP-0004
type: epic
title: E4 · USB compuesto HID+CDC, encendido y estado del PC
status: done
priority: critical
milestone: MESP-M-0002
author: mcp
labels: [firmware, usb, wake]
created: 2026-10-03T08:57:35Z
updated: 2026-10-04T22:00:42Z
closed: 2026-10-04T22:00:42Z
---

## Description
TinyUSB compuesto (teclado HID con remote wakeup + CDC ACM), encendido del PC por HID con fallback Wake-on-LAN (ADR-3) y máquina de estados del PC por fusión de señales (ADR-4).

## Acceptance Criteria
- DP 102 despierta el PC desde suspendido; desde apagado según resultado del spike S5 o vía WOL.
- DP 101 refleja off/sleep/booting/on_no_agent/on con latencia ≤10 s.
