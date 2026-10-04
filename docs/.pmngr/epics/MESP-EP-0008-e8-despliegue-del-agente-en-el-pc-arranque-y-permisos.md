---
id: MESP-EP-0008
type: epic
title: E8 · Despliegue del agente en el PC (arranque y permisos)
status: done
priority: medium
milestone: MESP-M-0003
author: mcp
labels: [agent, deploy, linux]
created: 2026-10-03T08:57:35Z
updated: 2026-10-04T22:00:42Z
closed: 2026-10-04T22:00:42Z
---

## Description
Script de instalación y arranque: unidad systemd, usuario de servicio, regla udev, polkit para poweroff/reboot, configuración BIOS documentada. Windows como opcional.

## Acceptance Criteria
- install.sh idempotente instala, habilita y arranca el servicio; uninstall limpia todo.
- El agente arranca al boot y está online en el dongle ≤20 s tras login-less boot.
