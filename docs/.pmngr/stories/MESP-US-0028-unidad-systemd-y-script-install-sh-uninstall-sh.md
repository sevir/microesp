---
id: MESP-US-0028
type: story
title: Unidad systemd y script install.sh/uninstall.sh
status: done
priority: high
parent: MESP-EP-0008
milestone: MESP-M-0003
author: mcp
labels: [deploy, linux]
estimate: 3
created: 2026-10-03T08:59:43Z
updated: 2026-10-04T00:09:05Z
started: 2026-10-03T10:36:36Z
closed: 2026-10-04T00:09:05Z
---

## Description
agent/deploy/systemd/microesp-agent.service (After=dev-serial…, Restart=always, usuario microesp, hardening: ProtectSystem=strict, NoNewPrivileges, DeviceAllow para ttyACM). install.sh idempotente: copia binario a /usr/local/bin, crea usuario en dialout, config por defecto, regla udev, polkit, enable --now.

## Acceptance Criteria
- Instalación limpia y reinstalación sin errores; uninstall deja el sistema como antes.
- `systemd-analyze security microesp-agent` exposición ≤4.0.
