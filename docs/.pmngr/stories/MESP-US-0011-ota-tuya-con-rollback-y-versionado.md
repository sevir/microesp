---
id: MESP-US-0011
type: story
title: OTA Tuya con rollback y versionado
status: cancelled
priority: medium
parent: MESP-EP-0003
milestone: MESP-M-0004
author: mcp
labels: [firmware, ota]
estimate: 3
created: 2026-10-03T08:58:23Z
updated: 2026-10-04T22:00:30Z
started: 2026-10-03T11:38:27Z
closed: 2026-10-04T22:00:30Z
---

## Description
Manejar TUYA_EVENT_UPGRADE_NOTIFY, escribir en partición OTA, marcar app válida tras health-check (cloud conectado + USB montado) o rollback automático.

## Acceptance Criteria
- OTA desde la plataforma Tuya completada y versión visible en app.
- Imagen corrupta/arranque fallido revierte a la versión anterior.
