---
id: MESP-US-0030
type: story
title: Guía de configuración BIOS Lenovo y SO para wake
status: done
priority: medium
parent: MESP-EP-0008
milestone: MESP-M-0003
author: mcp
labels: [deploy, docs, bios]
estimate: 2
created: 2026-10-03T08:59:43Z
updated: 2026-10-04T22:00:30Z
started: 2026-10-03T11:47:18Z
closed: 2026-10-04T22:00:30Z
---

## Description
docs/usuario/bios-lenovo.md con ajustes validados en el spike: Wake on keypress / USB wake, Always On USB / USB power in S4-S5, Deep sleep off, WOL on, Fast Startup (Windows) off, puerto recomendado; en Linux habilitar power/wakeup del dongle (regla udev ATTR{power/wakeup}="enabled") y WOL en NIC (ethtool/NetworkManager).

## Acceptance Criteria
- Siguiendo la guía en un equipo limpio, el encendido remoto funciona.
