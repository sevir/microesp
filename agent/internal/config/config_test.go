package config

import (
	"errors"
	"io/fs"
	"os"
	"path/filepath"
	"reflect"
	"runtime"
	"strings"
	"testing"
	"time"
)

func write(t *testing.T, body string) string {
	t.Helper()
	p := filepath.Join(t.TempDir(), "agent.toml")
	if err := os.WriteFile(p, []byte(body), 0o600); err != nil {
		t.Fatal(err)
	}
	return p
}

func TestDefaults(t *testing.T) {
	c := Default()
	if err := c.Validate(); err != nil {
		t.Fatal(err)
	}
	if c.Device != "auto" || c.KeyFile != DefaultKeyFile || c.Disks[0] != "/" ||
		c.TelemetryInterval.Duration != 10*time.Second || c.HeartbeatInterval.Duration != 5*time.Second || c.DryRun {
		t.Fatalf("unexpected defaults %+v", c)
	}
}

func TestLoadFile(t *testing.T) {
	p := write(t, `
device = "/dev/microesp"
key_file = "/tmp/k"
disks = ["/", "/home"]
telemetry_interval = "20s"
heartbeat_interval = "2s"
dry_run = true
power_backend = "logind-dbus"
`)
	c, err := Load(p)
	if err != nil {
		t.Fatal(err)
	}
	if c.Device != "/dev/microesp" || len(c.Disks) != 2 || c.TelemetryInterval.Duration != 20*time.Second ||
		c.HeartbeatInterval.Duration != 2*time.Second || !c.DryRun || c.PowerBackend != BackendLogind {
		t.Fatalf("%+v", c)
	}
	if err := c.Validate(); err != nil {
		t.Fatal(err)
	}
	if b, _ := c.TelemetryInterval.MarshalText(); string(b) != "20s" {
		t.Errorf("MarshalText = %s", b)
	}
}

func TestLoadErrors(t *testing.T) {
	if _, err := Load(filepath.Join(t.TempDir(), "missing.toml")); err == nil {
		t.Error("explicit missing file accepted")
	}
	if _, err := Load(write(t, `bogus = 1`)); err == nil || !strings.Contains(err.Error(), "unknown keys") {
		t.Errorf("unknown key: %v", err)
	}
	if _, err := Load(write(t, `telemetry_interval = "ten"`)); err == nil {
		t.Error("bad duration accepted")
	}
}

func TestLoadDefaultPathMissingOK(t *testing.T) {
	if _, err := os.Stat(DefaultPath); !errors.Is(err, fs.ErrNotExist) {
		t.Skip("default config exists (or is unreadable) on this host")
	}
	if _, err := Load(""); err != nil {
		t.Fatal(err)
	}
}

func TestEnv(t *testing.T) {
	env := map[string]string{
		"MICROESP_DEVICE":             "/dev/ttyACM9",
		"MICROESP_KEY_FILE":           "/k",
		"MICROESP_DISKS":              " /, /data ,,",
		"MICROESP_POWER_BACKEND":      "windows",
		"MICROESP_LOG_LEVEL":          "debug",
		"MICROESP_DRY_RUN":            "true",
		"MICROESP_TELEMETRY_INTERVAL": "3s",
		"MICROESP_HEARTBEAT_INTERVAL": "1s",
	}
	c := Default()
	if err := c.ApplyEnv(func(k string) string { return env[k] }); err != nil {
		t.Fatal(err)
	}
	if c.Device != "/dev/ttyACM9" || c.KeyFile != "/k" || strings.Join(c.Disks, "|") != "/|/data" ||
		c.PowerBackend != "windows" || !c.DryRun || c.TelemetryInterval.Duration != 3*time.Second ||
		c.HeartbeatInterval.Duration != time.Second || c.LogLevel != "debug" {
		t.Fatalf("%+v", c)
	}
	for k, v := range map[string]string{"MICROESP_DRY_RUN": "maybe", "MICROESP_HEARTBEAT_INTERVAL": "x"} {
		c := Default()
		if err := c.ApplyEnv(func(n string) string {
			if n == k {
				return v
			}
			return ""
		}); err == nil {
			t.Errorf("%s=%s accepted", k, v)
		}
	}
	c = Default()
	if err := c.ApplyEnv(nil); err != nil { // real environment, nothing set in tests
		t.Fatal(err)
	}
}

func TestValidate(t *testing.T) {
	c := Config{PowerBackend: "x", LogLevel: "loud"}
	err := c.Validate()
	if err == nil {
		t.Fatal("empty config valid")
	}
	for _, s := range []string{"device", "key_file", "disks", "telemetry_interval", "heartbeat_interval", "power_backend", "log_level"} {
		if !strings.Contains(err.Error(), s) {
			t.Errorf("missing %s in %v", s, err)
		}
	}
}

func TestExampleConfigParses(t *testing.T) {
	c, err := Load("../../deploy/agent.toml.example")
	if err != nil {
		t.Fatal(err)
	}
	if err := c.Validate(); err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(c, Default()) && runtime.GOOS != "windows" {
		t.Fatalf("example differs from defaults: %+v", c)
	}
}

const scriptsTOML = `
scripts_socket = "/run/microesp/scripts.sock"

[[scripts]]
id = "backup"
label = "Backup NAS"
command = "/usr/local/bin/backup.sh --full"
timeout = "30m"

[[scripts]]
id = "docker-up"
label = "Docker up"
command = "docker compose up -d"
`

func TestLoadScripts(t *testing.T) {
	c, err := Load(write(t, scriptsTOML))
	if err != nil {
		t.Fatal(err)
	}
	if err := c.Validate(); err != nil {
		t.Fatal(err)
	}
	want := []Script{
		{ID: "backup", Label: "Backup NAS", Command: "/usr/local/bin/backup.sh --full", Timeout: Duration{30 * time.Minute}},
		{ID: "docker-up", Label: "Docker up", Command: "docker compose up -d", Timeout: Duration{DefaultScriptTimeout}},
	}
	if c.ScriptsSocket != "/run/microesp/scripts.sock" {
		t.Errorf("scripts_socket = %q", c.ScriptsSocket)
	}
	if !reflect.DeepEqual(c.Scripts, want) {
		t.Fatalf("scripts = %+v", c.Scripts)
	}
	if _, err := Load(write(t, "[[scripts]]\nid = \"a\"\nlabel = \"A\"\ncommand = \"true\"\nuser = \"root\"\n")); err == nil ||
		!strings.Contains(err.Error(), "unknown keys") {
		t.Errorf("per-script user accepted: %v", err)
	}
}

func TestScriptsFilePermissions(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("permission check is Unix only")
	}
	p := write(t, scriptsTOML)
	for _, mode := range []os.FileMode{0o620, 0o602, 0o666} {
		if err := os.Chmod(p, mode); err != nil {
			t.Fatal(err)
		}
		if _, err := Load(p); !errors.Is(err, ErrInsecureConfig) {
			t.Errorf("mode %04o: err = %v, want ErrInsecureConfig", mode, err)
		}
	}
	for _, mode := range []os.FileMode{0o600, 0o640, 0o644} {
		if err := os.Chmod(p, mode); err != nil {
			t.Fatal(err)
		}
		if _, err := Load(p); err != nil {
			t.Errorf("mode %04o: %v", mode, err)
		}
	}
	// Without scripts a writable file is still accepted (unchanged behaviour).
	q := write(t, `device = "auto"`)
	if err := os.Chmod(q, 0o666); err != nil {
		t.Fatal(err)
	}
	if _, err := Load(q); err != nil {
		t.Errorf("no scripts: %v", err)
	}
	if err := CheckScriptsFile(filepath.Join(t.TempDir(), "missing")); err == nil {
		t.Error("missing file accepted")
	}
}

func TestValidateScripts(t *testing.T) {
	ok := Script{ID: "a", Label: "A", Command: "true", Timeout: Duration{time.Minute}}
	cases := map[string]func(c *Config){
		"at most 5": func(c *Config) {
			for _, id := range []string{"b", "c", "d", "e", "f"} {
				s := ok
				s.ID = id
				c.Scripts = append(c.Scripts, s)
			}
		},
		"id must match":    func(c *Config) { c.Scripts[0].ID = "Backup" },
		"duplicate id":     func(c *Config) { c.Scripts = append(c.Scripts, ok) },
		"empty label":      func(c *Config) { c.Scripts[0].Label = "" },
		"longer than":      func(c *Config) { c.Scripts[0].Label = strings.Repeat("x", 25) },
		"'\"' or '\\'":     func(c *Config) { c.Scripts[0].Label = `say "hi"` },
		"control":          func(c *Config) { c.Scripts[0].Label = "a\nb" },
		"command is empty": func(c *Config) { c.Scripts[0].Command = "  " },
		"timeout must be":  func(c *Config) { c.Scripts[0].Timeout.Duration = -time.Second },
		"scripts_socket":   func(c *Config) { c.ScriptsSocket = "run/x.sock" },
	}
	for want, mutate := range cases {
		c := Default()
		c.Scripts = []Script{ok}
		mutate(&c)
		if err := c.Validate(); err == nil || !strings.Contains(err.Error(), want) {
			t.Errorf("%s: err = %v", want, err)
		}
	}
	c := Default()
	c.Scripts = []Script{ok}
	if err := c.Validate(); err != nil {
		t.Fatal(err)
	}
}
