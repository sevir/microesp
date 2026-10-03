//go:build !windows

package power

import (
	"errors"
	"log/slog"
)

func newWindows(Runner, *slog.Logger) (Executor, error) {
	return nil, errors.New("power: windows backend is only available on Windows builds")
}
