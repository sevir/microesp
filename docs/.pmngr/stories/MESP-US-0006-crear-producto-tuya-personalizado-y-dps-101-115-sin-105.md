---
id: MESP-US-0006
type: story
title: Crear producto Tuya personalizado y DPs 101-115 (sin 105)
status: backlog
priority: high
parent: MESP-EP-0002
milestone: MESP-M-0002
author: mcp
labels: [tuya]
estimate: 3
created: 2026-10-03T08:58:23Z
updated: 2026-10-03T10:03:02Z
---

## Description
En platform.tuya.com crear producto (categoría base p. ej. "Switch"/custom, conexión Wi-Fi+BLE, TuyaOpen) y añadir DPs personalizados según [[00-analisis-arquitectura]] §5: pc_state, power_on, power_off, reboot, cpu_usage, mem_usage, disk_free, agent_online, wake_method, pc_uptime, pc_hostname, cmd_countdown, last_result, fault. DP 105 (usb_voltage) descartado y reservado.

## Acceptance Criteria
- PID anotado en docs (no secreto); export JSON del esquema DP en firmware/schema/dp.json.
- Tipos, rangos, escalas y modo (ro/rw) coinciden con la tabla del análisis.
