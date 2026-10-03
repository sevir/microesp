package power

// SAFETY: these tests never run a real power command. All executors are
// built with fakeRunner; ExecRunner is only exercised with true/false.

import (
	"context"
	"errors"
	"io"
	"log/slog"
	"runtime"
	"strings"
	"testing"
)

type call struct {
	name string
	args []string
}

type fakeRunner struct {
	calls []call
	fail  map[string]error
}

func (f *fakeRunner) run(_ context.Context, name string, args ...string) error {
	f.calls = append(f.calls, call{name, args})
	return f.fail[name]
}

var quiet = slog.New(slog.NewTextHandler(io.Discard, nil))

func TestCommandsBothOS(t *testing.T) {
	cases := []struct {
		f      CommandFunc
		action string
		want   string
	}{
		{SystemdCommand, Shutdown, "systemctl poweroff"},
		{SystemdCommand, Reboot, "systemctl reboot"},
		{LogindCommand, Shutdown, "busctl call org.freedesktop.login1 /org/freedesktop/login1 org.freedesktop.login1.Manager PowerOff b false"},
		{LogindCommand, Reboot, "busctl call org.freedesktop.login1 /org/freedesktop/login1 org.freedesktop.login1.Manager Reboot b false"},
		{WindowsCommand, Shutdown, "shutdown /s /t 0"},
		{WindowsCommand, Reboot, "shutdown /r /t 0"},
	}
	for _, c := range cases {
		fr := &fakeRunner{}
		e := &CommandExecutor{BackendName: "t", Command: c.f, Run: fr.run, Log: quiet}
		if err := e.Execute(context.Background(), c.action); err != nil {
			t.Fatal(err)
		}
		got := fr.calls[0].name + " " + strings.Join(fr.calls[0].args, " ")
		if got != c.want {
			t.Errorf("got %q want %q", got, c.want)
		}
		if err := e.Execute(context.Background(), "format_disk"); !errors.Is(err, ErrUnknownAction) {
			t.Errorf("unknown action: %v", err)
		}
		if len(fr.calls) != 1 {
			t.Errorf("unknown action ran a command")
		}
	}
}

func TestExecuteErrorAndNotify(t *testing.T) {
	boom := errors.New("polkit denied")
	fr := &fakeRunner{fail: map[string]error{"systemctl": boom, "wall": errors.New("no tty")}}
	e, err := newBackend("systemd", fr.run, quiet)
	if err != nil {
		t.Fatal(err)
	}
	e.Notify(context.Background(), "hola")
	if err := e.Execute(context.Background(), Shutdown); !errors.Is(err, boom) {
		t.Fatalf("err = %v", err)
	}
	if fr.calls[0].name != "wall" || fr.calls[0].args[0] != "hola" {
		t.Errorf("notify call %+v", fr.calls[0])
	}
	if e.Name() != "systemd" {
		t.Error(e.Name())
	}
	// No notify command configured: nothing runs.
	fr2 := &fakeRunner{}
	(&CommandExecutor{Command: WindowsCommand, Run: fr2.run}).Notify(context.Background(), "x")
	if len(fr2.calls) != 0 {
		t.Error("notify without NotifyCmd ran a command")
	}
}

func TestPreflight(t *testing.T) {
	e := &CommandExecutor{Command: SystemdCommand, LookPath: func(string) (string, error) { return "", errors.New("nope") }}
	if e.Preflight() == nil {
		t.Error("missing binary passed preflight")
	}
	e.LookPath = func(n string) (string, error) { return "/bin/" + n, nil }
	if err := e.Preflight(); err != nil {
		t.Error(err)
	}
	bad := &CommandExecutor{Command: func(string) (string, []string, error) { return "", nil, ErrUnknownAction }}
	if bad.Preflight() == nil {
		t.Error("bad command passed preflight")
	}
	// Default LookPath on a binary that surely exists.
	sh := &CommandExecutor{Command: func(string) (string, []string, error) { return "sh", nil, nil }}
	if runtime.GOOS != "windows" && sh.Preflight() != nil {
		t.Error("sh not found")
	}
}

func TestDryRun(t *testing.T) {
	ex, err := New("systemd", true, quiet)
	if err != nil {
		t.Fatal(err)
	}
	d := ex.(*DryRun)
	ctx := context.Background()
	if err := d.Execute(ctx, Shutdown); err != nil {
		t.Fatal(err)
	}
	if err := d.Execute(ctx, Reboot); err != nil {
		t.Fatal(err)
	}
	if d.Execute(ctx, "x") == nil {
		t.Error("unknown accepted")
	}
	d.Notify(ctx, "n")
	if strings.Join(d.Actions(), ",") != "shutdown,reboot" || len(d.Notes()) != 1 || d.Name() != "dry-run" {
		t.Fatalf("%v %v", d.Actions(), d.Notes())
	}
	(&DryRun{}).logger() // default logger path
}

func TestBackends(t *testing.T) {
	fr := &fakeRunner{}
	for _, b := range []string{"systemd", "logind-dbus"} {
		if _, err := newBackend(b, fr.run, quiet); err != nil {
			t.Error(b, err)
		}
	}
	if _, err := newBackend("nope", fr.run, quiet); err == nil {
		t.Error("unknown backend accepted")
	}
	_, err := newBackend("windows", fr.run, quiet)
	if (runtime.GOOS == "windows") != (err == nil) {
		t.Errorf("windows backend on %s: %v", runtime.GOOS, err)
	}
	// New without dry-run only constructs; it must not execute anything.
	if ex, err := New("systemd", false, quiet); err != nil || ex.Name() != "systemd" {
		t.Error(err)
	}
	(&CommandExecutor{}).logger()
}

func TestExecRunner(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip()
	}
	if err := ExecRunner(context.Background(), "true"); err != nil {
		t.Error(err)
	}
	if err := ExecRunner(context.Background(), "false"); err == nil {
		t.Error("false succeeded")
	}
}
