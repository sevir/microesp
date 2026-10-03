---
id: MESP-US-0027
type: story
title: Ejecución de apagado y reinicio recibidos del dongle
status: in_review
priority: high
parent: MESP-EP-0007
milestone: MESP-M-0003
author: mcp
labels: [agent, go, power]
estimate: 3
created: 2026-10-03T08:59:43Z
updated: 2026-10-03T10:36:36Z
started: 2026-10-03T10:18:52Z
---

## Description
internal/power: verificar HMAC/ventana/id único, enviar ack antes de ejecutar, notificar a sesiones (wall/notify-send opcional), ejecutar `systemctl poweroff|reboot` (Linux) o `shutdown /s|/r /t 0` (Windows). Modo dry-run para pruebas.

## Acceptance Criteria
- Comando no firmado o repetido → ack{ok:false,err}.
- Dry-run registra la acción sin ejecutarla; tests cubren ambos SO con ejecutor simulado.
