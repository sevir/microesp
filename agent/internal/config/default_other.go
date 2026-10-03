//go:build !windows

package config

// DefaultBackend is the power backend used when none is configured.
func DefaultBackend() string { return BackendSystemd }

// Default file locations.
const (
	DefaultPath    = "/etc/microesp/agent.toml"
	DefaultKeyFile = "/etc/microesp/agent.key"
)
