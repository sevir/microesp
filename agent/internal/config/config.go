// Package config loads the agent configuration from TOML with
// environment and flag overrides (precedence: defaults < file < env < flags).
package config

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/BurntSushi/toml"

	"github.com/microesp/agent/internal/proto"
)

// DeviceAuto selects USB discovery by VID/PID or product string.
const DeviceAuto = "auto"

// Power backends.
const (
	BackendSystemd = "systemd"
	BackendLogind  = "logind-dbus"
	BackendWindows = "windows"
)

// Duration is a time.Duration that decodes from strings such as "10s".
type Duration struct{ time.Duration }

// UnmarshalText implements encoding.TextUnmarshaler.
func (d *Duration) UnmarshalText(b []byte) error {
	v, err := time.ParseDuration(string(b))
	if err != nil {
		return err
	}
	d.Duration = v
	return nil
}

// MarshalText implements encoding.TextMarshaler.
func (d Duration) MarshalText() ([]byte, error) { return []byte(d.String()), nil }

// Config is the agent configuration.
type Config struct {
	Device            string   `toml:"device"`
	KeyFile           string   `toml:"key_file"`
	Disks             []string `toml:"disks"`
	TelemetryInterval Duration `toml:"telemetry_interval"`
	HeartbeatInterval Duration `toml:"heartbeat_interval"`
	DryRun            bool     `toml:"dry_run"`
	PowerBackend      string   `toml:"power_backend"`
	LogLevel          string   `toml:"log_level"`
	ScriptsSocket     string   `toml:"scripts_socket"`
	Scripts           []Script `toml:"scripts"`
}

// DefaultScriptTimeout applies to a script without timeout.
const DefaultScriptTimeout = 10 * time.Minute

// Script is a user script ([[scripts]] table) that the owner allows the
// dongle to start. Only ID and Label are sent to the dongle.
type Script struct {
	ID      string   `toml:"id"`
	Label   string   `toml:"label"`
	Command string   `toml:"command"`
	Timeout Duration `toml:"timeout"`
}

// ErrInsecureConfig is returned when the configuration defines scripts but
// can be modified by users other than its owner.
var ErrInsecureConfig = errors.New("config file is writable by group or others; refusing to load [[scripts]] (chmod go-w)")

// Default returns the built-in defaults.
func Default() Config {
	return Config{
		Device:            DeviceAuto,
		KeyFile:           DefaultKeyFile,
		Disks:             []string{"/"},
		TelemetryInterval: Duration{10 * time.Second},
		HeartbeatInterval: Duration{5 * time.Second},
		PowerBackend:      DefaultBackend(),
		LogLevel:          "info",
	}
}

// Load reads path over the defaults. If path is empty, DefaultPath is used
// and a missing file is not an error; an explicit path must exist.
func Load(path string) (Config, error) {
	cfg := Default()
	explicit := path != ""
	if !explicit {
		path = DefaultPath
	}
	md, err := toml.DecodeFile(path, &cfg)
	switch {
	case err == nil:
		if und := md.Undecoded(); len(und) > 0 {
			return cfg, fmt.Errorf("%s: unknown keys %v", path, und)
		}
	case !explicit && errors.Is(err, fs.ErrNotExist):
	default:
		return cfg, fmt.Errorf("config %s: %w", path, err)
	}
	if len(cfg.Scripts) > 0 {
		if err := CheckScriptsFile(path); err != nil {
			return cfg, err
		}
	}
	for i := range cfg.Scripts {
		if cfg.Scripts[i].Timeout.Duration == 0 {
			cfg.Scripts[i].Timeout.Duration = DefaultScriptTimeout
		}
	}
	return cfg, nil
}

// CheckScriptsFile refuses a configuration file that defines scripts and
// is writable by group or others: whoever can edit it can run commands as
// the service user. The check is skipped on Windows, where the installer
// restricts the ACL of the data directory instead.
func CheckScriptsFile(path string) error {
	if runtime.GOOS == "windows" {
		return nil
	}
	fi, err := os.Stat(path)
	if err != nil {
		return err
	}
	if perm := fi.Mode().Perm(); perm&0o022 != 0 {
		return fmt.Errorf("%s: %w (mode is %04o)", path, ErrInsecureConfig, perm)
	}
	return nil
}

// Getenv abstracts os.Getenv for tests.
type Getenv func(string) string

// ApplyEnv applies MICROESP_* environment overrides.
func (c *Config) ApplyEnv(getenv Getenv) error {
	if getenv == nil {
		getenv = os.Getenv
	}
	if v := getenv("MICROESP_DEVICE"); v != "" {
		c.Device = v
	}
	if v := getenv("MICROESP_KEY_FILE"); v != "" {
		c.KeyFile = v
	}
	if v := getenv("MICROESP_DISKS"); v != "" {
		c.Disks = SplitList(v)
	}
	if v := getenv("MICROESP_POWER_BACKEND"); v != "" {
		c.PowerBackend = v
	}
	if v := getenv("MICROESP_LOG_LEVEL"); v != "" {
		c.LogLevel = v
	}
	if v := getenv("MICROESP_DRY_RUN"); v != "" {
		b, err := strconv.ParseBool(v)
		if err != nil {
			return fmt.Errorf("MICROESP_DRY_RUN: %w", err)
		}
		c.DryRun = b
	}
	for name, dst := range map[string]*Duration{
		"MICROESP_TELEMETRY_INTERVAL": &c.TelemetryInterval,
		"MICROESP_HEARTBEAT_INTERVAL": &c.HeartbeatInterval,
	} {
		if v := getenv(name); v != "" {
			if err := dst.UnmarshalText([]byte(v)); err != nil {
				return fmt.Errorf("%s: %w", name, err)
			}
		}
	}
	return nil
}

// SplitList splits a comma separated list, dropping empty items.
func SplitList(s string) []string {
	var out []string
	for _, p := range strings.Split(s, ",") {
		if p = strings.TrimSpace(p); p != "" {
			out = append(out, p)
		}
	}
	return out
}

// Validate checks the configuration for consistency.
func (c *Config) Validate() error {
	var errs []error
	if c.Device == "" {
		errs = append(errs, errors.New("device must be a path or \"auto\""))
	}
	if c.KeyFile == "" {
		errs = append(errs, errors.New("key_file is empty"))
	}
	if len(c.Disks) == 0 {
		errs = append(errs, errors.New("disks is empty"))
	}
	if c.TelemetryInterval.Duration < time.Second {
		errs = append(errs, errors.New("telemetry_interval must be >= 1s"))
	}
	// The dongle marks the agent offline after 15 s without messages.
	if c.HeartbeatInterval.Duration < 100*time.Millisecond || c.HeartbeatInterval.Duration > 10*time.Second {
		errs = append(errs, errors.New("heartbeat_interval must be within 100ms..10s"))
	}
	switch c.PowerBackend {
	case BackendSystemd, BackendLogind, BackendWindows:
	default:
		errs = append(errs, fmt.Errorf("unknown power_backend %q", c.PowerBackend))
	}
	switch strings.ToLower(c.LogLevel) {
	case "debug", "info", "warn", "error":
	default:
		errs = append(errs, fmt.Errorf("unknown log_level %q", c.LogLevel))
	}
	if c.ScriptsSocket != "" && !strings.HasPrefix(c.ScriptsSocket, "/") {
		errs = append(errs, fmt.Errorf("scripts_socket must be an absolute path, got %q", c.ScriptsSocket))
	}
	errs = append(errs, c.validateScripts()...)
	return errors.Join(errs...)
}

func (c *Config) validateScripts() []error {
	var errs []error
	if len(c.Scripts) > proto.MaxScripts {
		errs = append(errs, fmt.Errorf("scripts: at most %d are allowed, got %d", proto.MaxScripts, len(c.Scripts)))
	}
	seen := map[string]bool{}
	for i, s := range c.Scripts {
		name := fmt.Sprintf("scripts[%d]", i)
		if s.ID != "" {
			name = fmt.Sprintf("scripts[%d] (%q)", i, s.ID)
		}
		if !proto.ValidScriptID(s.ID) {
			errs = append(errs, fmt.Errorf("%s: id must match ^[a-z0-9_-]{1,12}$", name))
		} else if seen[s.ID] {
			errs = append(errs, fmt.Errorf("%s: duplicate id", name))
		}
		seen[s.ID] = true
		if why := proto.CheckScriptLabel(s.Label); why != "" {
			errs = append(errs, fmt.Errorf("%s: %s (1..%d bytes, no control characters, no '\"' or '\\')", name, why, proto.MaxScriptLabelLen))
		}
		if strings.TrimSpace(s.Command) == "" {
			errs = append(errs, fmt.Errorf("%s: command is empty", name))
		}
		if s.Timeout.Duration < time.Second {
			errs = append(errs, fmt.Errorf("%s: timeout must be >= 1s", name))
		}
	}
	return errs
}
