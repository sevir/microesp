//go:build windows

package power

import "log/slog"

func newWindows(run Runner, log *slog.Logger) (Executor, error) {
	return &CommandExecutor{BackendName: "windows", Command: WindowsCommand, Run: run, Log: log}, nil
}
