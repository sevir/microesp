---
id: MESP-US-0024
type: story
title: Esqueleto del módulo Go, CLI y configuración
status: in_review
priority: high
parent: MESP-EP-0007
milestone: MESP-M-0003
author: mcp
labels: [agent, go]
estimate: 2
created: 2026-10-03T08:59:43Z
updated: 2026-10-03T10:36:36Z
started: 2026-10-03T10:18:52Z
---

## Description
agent/ con go.mod `microesp/agent`, cmd/microesp-agent (subcomandos run, pair, status, version), internal/{config,link,telemetry,power,log}. Config TOML/YAML en /etc/microesp/agent.toml con overrides por flags/env; logging slog a journald.

## Acceptance Criteria
- `go build ./...`, `go vet`, golangci-lint limpios.
- `microesp-agent version` muestra versión inyectada por ldflags.
