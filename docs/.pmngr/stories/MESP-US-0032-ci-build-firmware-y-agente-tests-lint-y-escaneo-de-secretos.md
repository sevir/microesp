---
id: MESP-US-0032
type: story
title: "CI: build firmware y agente, tests, lint y escaneo de secretos"
status: done
priority: medium
parent: MESP-EP-0009
milestone: MESP-M-0004
author: mcp
labels: [ci]
estimate: 3
created: 2026-10-03T08:59:43Z
updated: 2026-10-03T13:30:58Z
started: 2026-10-03T11:47:18Z
closed: 2026-10-03T13:30:58Z
---

## Description
Pipeline (GitHub Actions o similar): contenedor TuyaOpen/IDF → firmware .bin; Go test -race, golangci-lint, cobertura; tests host del firmware; gitleaks; artefactos por tag (goreleaser para el agente).

## Acceptance Criteria
- PR bloqueado si falla cualquier job.
- Tag vX.Y.Z publica firmware y agente linux-amd64/arm64 con checksums.
