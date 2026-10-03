---
id: MESP-US-0007
type: story
title: Obtener licencias dev (UUID/AuthKey) y gestión segura de credenciales
status: backlog
priority: high
parent: MESP-EP-0002
milestone: MESP-M-0002
author: mcp
labels: [tuya, security]
estimate: 1
created: 2026-10-03T08:58:23Z
updated: 2026-10-03T08:58:23Z
---

## Description
Reclamar las 2 licencias gratuitas del producto. Guardarlas fuera del repo (gestor de secretos / fichero .env ignorado) y escribirlas en el dongle con el comando CLI `auth` o tyutool.

## Acceptance Criteria
- Ninguna credencial en git (verificado con grep/gitleaks en CI).
- Procedimiento de provisión documentado.
