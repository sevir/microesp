// Package power executes shutdown/reboot actions on the host.
//
// Every real backend shells out to a system tool (systemctl, busctl,
// shutdown.exe) through a Runner so tests can substitute a fake one.
package power

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"os/exec"
	"strings"
	"sync"
	"time"
)

// Actions.
const (
	Shutdown = "shutdown"
	Reboot   = "reboot"
)

// ErrUnknownAction is returned for actions other than shutdown/reboot.
var ErrUnknownAction = errors.New("power: unknown action")

// Executor performs power actions.
type Executor interface {
	// Name identifies the backend in logs.
	Name() string
	// Execute performs action (Shutdown or Reboot).
	Execute(ctx context.Context, action string) error
	// Notify informs logged-in users; best-effort, never fails.
	Notify(ctx context.Context, msg string)
}

// Preflighter is implemented by executors that can tell in advance that
// Execute would fail (e.g. missing binary). The agent then answers the
// command with ack{ok:false,err:"exec_failed"} instead of ack{ok:true}.
type Preflighter interface {
	Preflight() error
}

// Runner runs a command and returns an error including its output.
type Runner func(ctx context.Context, name string, args ...string) error

// ExecRunner is the real Runner based on os/exec.
func ExecRunner(ctx context.Context, name string, args ...string) error {
	out, err := exec.CommandContext(ctx, name, args...).CombinedOutput()
	if err != nil {
		return fmt.Errorf("%s %s: %w: %s", name, strings.Join(args, " "), err, strings.TrimSpace(string(out)))
	}
	return nil
}

// CommandFunc maps an action to the command line that performs it.
type CommandFunc func(action string) (name string, args []string, err error)

// SystemdCommand uses `systemctl poweroff|reboot` (logind + polkit, no sudo).
func SystemdCommand(action string) (string, []string, error) {
	switch action {
	case Shutdown:
		return "systemctl", []string{"poweroff"}, nil
	case Reboot:
		return "systemctl", []string{"reboot"}, nil
	}
	return "", nil, ErrUnknownAction
}

// LogindCommand calls logind's PowerOff/Reboot over D-Bus via busctl
// (interactive=false, so polkit must grant it without prompting).
func LogindCommand(action string) (string, []string, error) {
	method := ""
	switch action {
	case Shutdown:
		method = "PowerOff"
	case Reboot:
		method = "Reboot"
	default:
		return "", nil, ErrUnknownAction
	}
	return "busctl", []string{"call", "org.freedesktop.login1", "/org/freedesktop/login1",
		"org.freedesktop.login1.Manager", method, "b", "false"}, nil
}

// WindowsCommand uses `shutdown /s|/r /t 0`.
func WindowsCommand(action string) (string, []string, error) {
	switch action {
	case Shutdown:
		return "shutdown", []string{"/s", "/t", "0"}, nil
	case Reboot:
		return "shutdown", []string{"/r", "/t", "0"}, nil
	}
	return "", nil, ErrUnknownAction
}

// CommandExecutor runs the command produced by Command.
type CommandExecutor struct {
	BackendName string
	Command     CommandFunc
	Run         Runner
	// NotifyCmd builds the best-effort user notification command; nil
	// disables notifications.
	NotifyCmd func(msg string) (string, []string)
	// LookPath checks binaries in Preflight; defaults to exec.LookPath.
	LookPath func(string) (string, error)
	Log      *slog.Logger
}

// WallNotify notifies terminals with wall(1).
func WallNotify(msg string) (string, []string) { return "wall", []string{msg} }

// Name implements Executor.
func (e *CommandExecutor) Name() string { return e.BackendName }

// Preflight checks that the backend binary is available.
func (e *CommandExecutor) Preflight() error {
	name, _, err := e.Command(Shutdown)
	if err != nil {
		return err
	}
	lp := e.LookPath
	if lp == nil {
		lp = exec.LookPath
	}
	_, err = lp(name)
	return err
}

// Execute implements Executor.
func (e *CommandExecutor) Execute(ctx context.Context, action string) error {
	name, args, err := e.Command(action)
	if err != nil {
		return err
	}
	e.logger().Warn("executing power action", "backend", e.BackendName, "action", action,
		"cmd", name+" "+strings.Join(args, " "))
	return e.Run(ctx, name, args...)
}

// Notify implements Executor.
func (e *CommandExecutor) Notify(ctx context.Context, msg string) {
	if e.NotifyCmd == nil {
		return
	}
	ctx, cancel := context.WithTimeout(ctx, 3*time.Second)
	defer cancel()
	name, args := e.NotifyCmd(msg)
	if err := e.Run(ctx, name, args...); err != nil {
		e.logger().Debug("user notification failed (best-effort)", "err", err)
	}
}

func (e *CommandExecutor) logger() *slog.Logger {
	if e.Log == nil {
		return slog.Default()
	}
	return e.Log
}

// DryRun only logs and records the actions it is asked to perform.
type DryRun struct {
	Log *slog.Logger

	mu      sync.Mutex
	actions []string
	notes   []string
}

// Name implements Executor.
func (*DryRun) Name() string { return "dry-run" }

// Execute implements Executor.
func (d *DryRun) Execute(_ context.Context, action string) error {
	if action != Shutdown && action != Reboot {
		return ErrUnknownAction
	}
	d.mu.Lock()
	d.actions = append(d.actions, action)
	d.mu.Unlock()
	d.logger().Warn("DRY-RUN: power action not executed", "action", action)
	return nil
}

// Notify implements Executor.
func (d *DryRun) Notify(_ context.Context, msg string) {
	d.mu.Lock()
	d.notes = append(d.notes, msg)
	d.mu.Unlock()
	d.logger().Info("DRY-RUN: notification", "msg", msg)
}

// Actions returns the actions recorded so far.
func (d *DryRun) Actions() []string {
	d.mu.Lock()
	defer d.mu.Unlock()
	return append([]string(nil), d.actions...)
}

// Notes returns the notifications recorded so far.
func (d *DryRun) Notes() []string {
	d.mu.Lock()
	defer d.mu.Unlock()
	return append([]string(nil), d.notes...)
}

func (d *DryRun) logger() *slog.Logger {
	if d.Log == nil {
		return slog.Default()
	}
	return d.Log
}

// New returns the executor for backend; dryRun overrides it with DryRun.
func New(backend string, dryRun bool, log *slog.Logger) (Executor, error) {
	if dryRun {
		return &DryRun{Log: log}, nil
	}
	return newBackend(backend, ExecRunner, log)
}

func newBackend(backend string, run Runner, log *slog.Logger) (Executor, error) {
	switch backend {
	case "systemd":
		return &CommandExecutor{BackendName: backend, Command: SystemdCommand, Run: run, NotifyCmd: WallNotify, Log: log}, nil
	case "logind-dbus":
		return &CommandExecutor{BackendName: backend, Command: LogindCommand, Run: run, NotifyCmd: WallNotify, Log: log}, nil
	case "windows":
		return newWindows(run, log)
	}
	return nil, fmt.Errorf("power: unknown backend %q", backend)
}
