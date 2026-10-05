//go:build windows

package scripts

import (
	"context"
	"os"
	"os/exec"
	"strconv"
	"syscall"
)

// command runs line with "cmd /C". The command line is passed verbatim
// (no Go argument quoting) so it behaves as typed in a console. On timeout
// the whole process tree is killed with taskkill /T /F.
func command(ctx context.Context, line string) *exec.Cmd {
	shell := os.Getenv("ComSpec")
	if shell == "" {
		shell = "cmd.exe"
	}
	cmd := exec.CommandContext(ctx, shell)
	cmd.SysProcAttr = &syscall.SysProcAttr{CmdLine: `cmd.exe /C ` + line}
	cmd.Cancel = func() error {
		kill := exec.Command("taskkill", "/T", "/F", "/PID", strconv.Itoa(cmd.Process.Pid))
		if err := kill.Run(); err != nil {
			return cmd.Process.Kill()
		}
		return nil
	}
	return cmd
}
