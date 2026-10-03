package config

import (
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
	if _, err := os.Stat(DefaultPath); err == nil {
		t.Skip("default config exists on this host")
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
