---
id: MESP-US-0023
type: story
title: Emparejado agente↔dongle y gestión del token compartido
status: in_review
priority: medium
parent: MESP-EP-0006
milestone: MESP-M-0003
author: mcp
labels: [protocol, security]
estimate: 3
created: 2026-10-03T08:59:05Z
updated: 2026-10-03T11:57:34Z
started: 2026-10-03T11:38:27Z
---

## Description
Primer arranque: `microesp-agent pair` pide código de 6 dígitos mostrado en la pantalla del dongle; derivar token (HKDF) y guardarlo en NVS (dongle) y /etc/microesp/agent.key (0600). Comando para revocar/re-emparejar.

## Acceptance Criteria
- Sin token válido el dongle ignora tele y no envía cmd.
- Re-emparejado invalida el token anterior.
