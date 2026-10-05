//go:build !windows

package scripts

import (
	"context"
	"os/exec"
	"syscall"
)

// command runs line with "sh -c" in its own process group, so that a
// timeout kills the script and everything it started.
func command(ctx context.Context, line string) *exec.Cmd {
	cmd := exec.CommandContext(ctx, "/bin/sh", "-c", line)
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	cmd.Cancel = func() error {
		// Negative pid = the whole process group (pgid == pid of sh).
		return syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL)
	}
	return cmd
}
