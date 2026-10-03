---
id: MESP-US-0005
type: story
title: "Entorno de desarrollo: TuyaOpen, ESP-IDF, Go y acceso serie"
status: done
priority: high
parent: MESP-EP-0001
milestone: MESP-M-0001
author: mcp
labels: [tooling]
estimate: 2
created: 2026-10-03T08:58:23Z
updated: 2026-10-03T10:59:23Z
started: 2026-10-03T10:18:52Z
closed: 2026-10-03T10:59:23Z
---

## Description
Preparar toolchain reproducible: clonar TuyaOpen, tos.py check, ESP-IDF que use TuyaOpen, esptool, Go ≥1.23. Acceso a /dev/ttyACM0 ya resuelto con scripts/setup-serial-access.sh (grupo dialout + regla udev 303a).

## Acceptance Criteria
- docs/dev-setup.md con pasos verificados en Linux.
- Procedimiento de modo descarga (BOOT al enchufar) y de restauración del backup de fábrica documentado.
