//go:build !linux

package main

import (
	"fmt"
	"log/slog"

	"github.com/microesp/agent/internal/config"
)

// cmdScriptsRunner is Linux only: on Windows the service runs the scripts
// itself.
func cmdScriptsRunner(d deps, _ config.Config, _ string, _ *slog.Logger) int {
	fmt.Fprintln(d.stderr, "scripts-runner: not supported on this OS (Linux only)")
	return 1
}
