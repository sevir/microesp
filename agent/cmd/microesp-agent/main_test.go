package main

// SAFETY: every test injects a fake serial opener (net.Pipe dongle) and a
// dry-run executor; the test fails if a non-dry-run executor is requested.

import (
	"bytes"
	"context"
	"encoding/hex"
	"errors"
	"io"
	"io/fs"
	"log/slog"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/microesp/agent/internal/config"
	"github.com/microesp/agent/internal/link"
	"github.com/microesp/agent/internal/link/dongletest"
	"github.com/microesp/agent/internal/power"
	"github.com/microesp/agent/internal/proto"
	"github.com/microesp/agent/internal/telemetry"
)

type env struct {
	t       *testing.T
	dir     string
	cfg     string
	keyFile string
	out     bytes.Buffer
	errb    bytes.Buffer
	stdin   string

	mu      sync.Mutex
	dongles []*dongletest.Dongle
	key     []byte
	code    string
	dry     *power.DryRun
	ctx     context.Context
	cancel  context.CancelFunc
	found   bool
}

func newEnv(t *testing.T) *env {
	dir := t.TempDir()
	e := &env{t: t, dir: dir, keyFile: filepath.Join(dir, "agent.key"), found: true}
	e.cfg = filepath.Join(dir, "agent.toml")
	body := "device = \"auto\"\nkey_file = \"" + filepath.ToSlash(e.keyFile) + "\"\ndry_run = true\nheartbeat_interval = \"100ms\"\ntelemetry_interval = \"1s\"\nlog_level = \"error\"\n"
	if err := os.WriteFile(e.cfg, []byte(body), 0o600); err != nil {
		t.Fatal(err)
	}
	e.ctx, e.cancel = context.WithCancel(context.Background())
	return e
}

func (e *env) deps() deps {
	return deps{
		stdin:  strings.NewReader(e.stdin),
		stdout: &e.out,
		stderr: &e.errb,
		getenv: func(string) string { return "" },
		list: func() ([]link.PortInfo, error) {
			if !e.found {
				return nil, nil
			}
			return []link.PortInfo{{Name: "/dev/fake-microesp", IsUSB: true, VID: "303a", PID: "4002", Serial: "MESP-907069f662dc", Product: "MicroESP"}}, nil
		},
		open: func(string) (io.ReadWriteCloser, error) {
			e.mu.Lock()
			defer e.mu.Unlock()
			rw, d := dongletest.New(e.key, dongletest.Normal)
			d.PairCode = e.code
			e.dongles = append(e.dongles, d)
			return rw, nil
		},
		newExecutor: func(backend string, dryRun bool, log *slog.Logger) (power.Executor, error) {
			if !dryRun {
				e.t.Fatal("SAFETY: non dry-run executor requested in test")
			}
			e.mu.Lock()
			defer e.mu.Unlock()
			e.dry = &power.DryRun{Log: log}
			return e.dry, nil
		},
		collector: func([]string, *slog.Logger) telemetry.Collector {
			f := &telemetry.Fake{}
			f.Set(telemetry.Snapshot{CPU: 1, Mem: 2, DiskFree: 3, Uptime: 4}, nil)
			return f
		},
		hostInfo: func() (telemetry.HostInfo, error) { return telemetry.HostInfo{Hostname: "test"}, nil },
		ctx:      func() (context.Context, context.CancelFunc) { return e.ctx, e.cancel },
	}
}

func (e *env) last() *dongletest.Dongle {
	e.mu.Lock()
	defer e.mu.Unlock()
	if len(e.dongles) == 0 {
		return nil
	}
	return e.dongles[len(e.dongles)-1]
}

func waitFor(t *testing.T, what string, cond func() bool) {
	t.Helper()
	deadline := time.Now().Add(3 * time.Second)
	for !cond() {
		if time.Now().After(deadline) {
			t.Fatalf("timeout waiting for %s", what)
		}
		time.Sleep(2 * time.Millisecond)
	}
}

func TestVersionAndUsage(t *testing.T) {
	e := newEnv(t)
	version = "1.2.3"
	if rc := runMain([]string{"version"}, e.deps()); rc != 0 || !strings.Contains(e.out.String(), "microesp-agent 1.2.3") {
		t.Fatal(rc, e.out.String())
	}
	if rc := runMain([]string{"bogus"}, e.deps()); rc != 2 {
		t.Error("unknown subcommand", rc)
	}
	if rc := runMain([]string{"help"}, e.deps()); rc != 0 || !strings.Contains(e.errb.String(), "Uso:") {
		t.Error("help")
	}
	if rc := runMain([]string{"run", "-h"}, e.deps()); rc != 0 {
		t.Error("-h", rc)
	}
	if rc := runMain([]string{"run", "--nope"}, e.deps()); rc != 2 {
		t.Error("bad flag", rc)
	}
	if rc := runMain([]string{"run", "--config", e.cfg, "--power-backend", "amiga"}, e.deps()); rc != 2 {
		t.Error("invalid backend accepted", rc)
	}
	if rc := runMain([]string{"run", "--config", filepath.Join(e.dir, "missing.toml")}, e.deps()); rc != 2 {
		t.Error("missing config accepted", rc)
	}
	var b boolFlag
	if b.Set("maybe") == nil || b.Set("no") != nil || b.String() != "false" || !b.IsBoolFlag() {
		t.Error("boolFlag")
	}
}

func TestPairThenRun(t *testing.T) {
	e := newEnv(t)
	e.code = "482913"
	e.stdin = "482913\n"
	if rc := runMain([]string{"pair", "--config", e.cfg, "--owner", ""}, e.deps()); rc != 0 {
		t.Fatalf("pair rc=%d stderr=%s", rc, e.errb.String())
	}
	key, err := config.LoadKey(e.keyFile)
	if err != nil || !bytes.Equal(key, e.last().PairedKey()) {
		t.Fatalf("key %x err %v", key, err)
	}
	e.key = key

	// status with handshake using the stored key.
	e.out.Reset()
	if rc := runMain([]string{"status", "--config", e.cfg, "--handshake"}, e.deps()); rc != 0 {
		t.Fatalf("status rc=%d out=%s", rc, e.out.String())
	}
	if !strings.Contains(e.out.String(), "sesión:                  OK") || !strings.Contains(e.out.String(), "clave:                   OK") {
		t.Fatal(e.out.String())
	}

	// run (dry-run) until a command is executed, then stop.
	done := make(chan int, 1)
	args := []string{"--dry-run"}
	if _, err := os.Stat(config.DefaultPath); !errors.Is(err, fs.ErrNotExist) {
		// The agent is installed on this host: do not read its config.
		args = append(args, "--config", e.cfg)
	}
	go func() { done <- runMain(args, withConfigEnv(e)) }()
	waitFor(t, "ready", func() bool {
		e.mu.Lock()
		n := len(e.dongles)
		e.mu.Unlock()
		d := e.last()
		return n >= 3 && d.Ready()
	})
	d := e.last()
	if err := d.Send(d.SignedCmd(1, proto.ActionShutdown)); err != nil {
		t.Fatal(err)
	}
	waitFor(t, "dry-run action", func() bool {
		e.mu.Lock()
		dry := e.dry
		e.mu.Unlock()
		return dry != nil && len(dry.Actions()) == 1
	})
	e.cancel()
	if rc := <-done; rc != 0 {
		t.Fatalf("run rc=%d %s", rc, e.errb.String())
	}
}

// withConfigEnv points MICROESP_* env at the test config via getenv, so the
// default (flag-less) invocation is also exercised.
func withConfigEnv(e *env) deps {
	d := e.deps()
	d.getenv = func(k string) string {
		switch k {
		case "MICROESP_KEY_FILE":
			return e.keyFile
		case "MICROESP_DRY_RUN":
			return "true"
		case "MICROESP_HEARTBEAT_INTERVAL":
			return "100ms"
		case "MICROESP_LOG_LEVEL":
			return "error"
		}
		return ""
	}
	return d
}

func TestPairFailures(t *testing.T) {
	e := newEnv(t)
	e.code = "111111"
	if rc := runMain([]string{"pair", "--config", e.cfg, "--code", "222222"}, e.deps()); rc != 1 {
		t.Error("wrong code rc", rc)
	}
	if !strings.Contains(e.errb.String(), "rechazado") {
		t.Error(e.errb.String())
	}
	if _, err := os.Stat(e.keyFile); err == nil {
		t.Fatal("key written after failed pairing")
	}
	if rc := runMain([]string{"pair", "--config", e.cfg, "--code", "12a456"}, e.deps()); rc != 2 {
		t.Error("invalid code rc", rc)
	}
	e.stdin = ""
	if rc := runMain([]string{"pair", "--config", e.cfg}, e.deps()); rc != 1 {
		t.Error("empty stdin rc", rc)
	}
	e.found = false
	if rc := runMain([]string{"pair", "--config", e.cfg, "--code", "111111"}, e.deps()); rc != 1 {
		t.Error("no device rc", rc)
	}
}

func TestStatusNoDevice(t *testing.T) {
	e := newEnv(t)
	e.found = false
	if rc := runMain([]string{"status", "--config", e.cfg}, e.deps()); rc != 1 {
		t.Error("rc", rc)
	}
	if !strings.Contains(e.out.String(), "ninguno detectado") || !strings.Contains(e.out.String(), "NO VÁLIDA") {
		t.Error(e.out.String())
	}
	// Device present but wrong key: handshake fails.
	e.found = true
	e.key = bytes.Repeat([]byte{9}, 32)
	_ = os.WriteFile(e.keyFile, []byte(hex.EncodeToString(bytes.Repeat([]byte{8}, 32))), 0o600)
	e.out.Reset()
	if rc := runMain([]string{"status", "--config", e.cfg, "--handshake"}, e.deps()); rc != 1 {
		t.Error("rc", rc)
	}
	if !strings.Contains(e.out.String(), "FALLO") {
		t.Error(e.out.String())
	}
}

func TestRunExecutorError(t *testing.T) {
	e := newEnv(t)
	d := e.deps()
	d.newExecutor = func(string, bool, *slog.Logger) (power.Executor, error) { return nil, io.EOF }
	if rc := runMain([]string{"run", "--config", e.cfg}, d); rc != 1 {
		t.Error(rc)
	}
	if realDeps().stdout != os.Stdout || osName() == "" {
		t.Error("realDeps")
	}
}

// Regression: Group=dialout replaced the primary group microesp, so the
// service could not traverse /etc/microesp (root:microesp 0750) nor read
// agent.toml (0640). The unit must also keep AF_UNIX for D-Bus (systemctl).
func TestSystemdUnitGroupsAndDBus(t *testing.T) {
	b, err := os.ReadFile(filepath.Join("..", "..", "deploy", "systemd", "microesp-agent.service"))
	if err != nil {
		t.Fatal(err)
	}
	var hasUnix, hasDialout bool
	for _, l := range strings.Split(string(b), "\n") {
		l = strings.TrimSpace(l)
		switch {
		case strings.HasPrefix(l, "Group="):
			t.Errorf("unit overrides the primary group: %q", l)
		case strings.HasPrefix(l, "SupplementaryGroups=") && strings.Contains(l, "dialout"):
			hasDialout = true
		case strings.HasPrefix(l, "RestrictAddressFamilies=") && strings.Contains(l, "AF_UNIX"):
			hasUnix = true
		}
	}
	if !hasUnix || !hasDialout {
		t.Errorf("AF_UNIX=%v dialout=%v", hasUnix, hasDialout)
	}
}

func TestRunWithScripts(t *testing.T) {
	e := newEnv(t)
	e.key = bytes.Repeat([]byte{7}, 32)
	if err := config.SaveKey(e.keyFile, e.key, ""); err != nil {
		t.Fatal(err)
	}
	body, _ := os.ReadFile(e.cfg)
	body = append(body, "\n[[scripts]]\nid = \"backup\"\nlabel = \"Backup NAS\"\ncommand = \"exit 0\"\n"...)
	if err := os.WriteFile(e.cfg, body, 0o600); err != nil {
		t.Fatal(err)
	}
	e.out.Reset()
	if rc := runMain([]string{"status", "--config", e.cfg}, e.deps()); rc != 0 || !strings.Contains(e.out.String(), "scripts:                 1 [backup]") {
		t.Fatalf("status rc=%d %s", rc, e.out.String())
	}

	done := make(chan int, 1)
	go func() { done <- runMain([]string{"run", "--config", e.cfg}, e.deps()) }()
	waitFor(t, "ready", func() bool { d := e.last(); return d != nil && d.Ready() })
	d := e.last()
	waitFor(t, "scripts list", func() bool {
		for _, m := range d.Received() {
			if s, ok := m.(*proto.Scripts); ok && len(s.List) == 1 && s.List[0].ID == "backup" && s.List[0].Label == "Backup NAS" {
				return true
			}
		}
		return false
	})
	if err := d.Send(d.SignedCmd(1, proto.ScriptAction("backup"))); err != nil {
		t.Fatal(err)
	}
	waitFor(t, "ack", func() bool { a, ok := d.Ack(1); return ok && a.OK })
	e.cancel()
	if rc := <-done; rc != 0 {
		t.Fatalf("run rc=%d %s", rc, e.errb.String())
	}

	// A config with scripts that others can modify is refused.
	if runtime.GOOS != "windows" {
		if err := os.Chmod(e.cfg, 0o666); err != nil {
			t.Fatal(err)
		}
		e.errb.Reset()
		if rc := runMain([]string{"run", "--config", e.cfg}, e.deps()); rc != 2 || !strings.Contains(e.errb.String(), "writable by group or others") {
			t.Fatalf("insecure config rc=%d %s", rc, e.errb.String())
		}
	}
}

// The runner units are templates rendered by install.sh --scripts-user and
// must keep the access model of the README (socket NAME:microesp 0660,
// runner in the microesp group to read agent.toml).
func TestScriptsRunnerUnits(t *testing.T) {
	read := func(name string) string {
		b, err := os.ReadFile(filepath.Join("..", "..", "deploy", "systemd", name))
		if err != nil {
			t.Fatal(err)
		}
		return string(b)
	}
	sock, svc := read("microesp-scripts.socket"), read("microesp-scripts.service")
	for _, want := range []string{"ListenStream=/run/microesp/scripts.sock", "SocketUser=@SCRIPTS_USER@", "SocketGroup=microesp", "SocketMode=0660", "RemoveOnStop=yes"} {
		if !strings.Contains(sock, "\n"+want+"\n") {
			t.Errorf("socket unit lacks %q", want)
		}
	}
	for _, want := range []string{"User=@SCRIPTS_USER@", "SupplementaryGroups=microesp", "Restart=on-failure", "ExecStart=/usr/local/bin/microesp-agent scripts-runner --config /etc/microesp/agent.toml"} {
		if !strings.Contains(svc, "\n"+want+"\n") {
			t.Errorf("service unit lacks %q", want)
		}
	}
}
