---
id: MESP-US-0029
type: story
title: Permisos mínimos para poweroff/reboot (polkit) y acceso serie
status: done
priority: high
parent: MESP-EP-0008
milestone: MESP-M-0003
author: mcp
labels: [deploy, linux, security]
estimate: 2
created: 2026-10-03T08:59:43Z
updated: 2026-10-04T00:09:05Z
started: 2026-10-03T10:36:36Z
closed: 2026-10-04T00:09:05Z
---

## Description
Regla polkit que permite solo org.freedesktop.login1.power-off/reboot(-multiple-sessions) al usuario microesp; regla udev para el VID/PID del firmware MicroESP (grupo dialout, symlink /dev/microesp).

## Acceptance Criteria
- El servicio apaga/reinicia sin root y sin poder ejecutar otras acciones privilegiadas.
