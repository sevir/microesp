//go:build windows

package config

// DefaultBackend is the power backend used when none is configured.
func DefaultBackend() string { return BackendWindows }

// Default file locations.
const (
	DefaultPath    = `C:\ProgramData\MicroESP\agent.toml`
	DefaultKeyFile = `C:\ProgramData\MicroESP\agent.key`
)
