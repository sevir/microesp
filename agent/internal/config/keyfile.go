package config

import (
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"os/user"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
)

// KeyBytes is the shared key length.
const KeyBytes = 32

// ErrInsecureKeyFile is returned when the key file is group/world accessible.
var ErrInsecureKeyFile = errors.New("key file permissions too open (want 0600)")

// LoadKey reads a hex-encoded 32-byte key. On Unix it refuses files
// readable by group or others.
func LoadKey(path string) ([]byte, error) {
	fi, err := os.Stat(path)
	if err != nil {
		return nil, err
	}
	if runtime.GOOS != "windows" && fi.Mode().Perm()&0o077 != 0 {
		return nil, fmt.Errorf("%s: %w (is %04o)", path, ErrInsecureKeyFile, fi.Mode().Perm())
	}
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	key, err := hex.DecodeString(strings.TrimSpace(string(b)))
	if err != nil || len(key) != KeyBytes {
		return nil, fmt.Errorf("%s: not a %d-byte hex key", path, KeyBytes)
	}
	return key, nil
}

// SaveKey atomically writes key as lowercase hex with mode 0600. If owner
// is non-empty and the process runs as root, the file is chowned to that
// user (the service account) so the service can read it.
func SaveKey(path string, key []byte, owner string) error {
	if len(key) != KeyBytes {
		return fmt.Errorf("key must be %d bytes", KeyBytes)
	}
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0o750); err != nil {
		return err
	}
	tmp, err := os.CreateTemp(dir, ".agent.key.*")
	if err != nil {
		return err
	}
	defer os.Remove(tmp.Name()) // no-op after rename
	if err := tmp.Chmod(0o600); err != nil {
		tmp.Close()
		return err
	}
	if _, err := tmp.WriteString(hex.EncodeToString(key) + "\n"); err != nil {
		tmp.Close()
		return err
	}
	if err := tmp.Sync(); err != nil {
		tmp.Close()
		return err
	}
	if err := tmp.Close(); err != nil {
		return err
	}
	if owner != "" && os.Geteuid() == 0 {
		if err := chownTo(tmp.Name(), owner); err != nil {
			return err
		}
	}
	return os.Rename(tmp.Name(), path)
}

func chownTo(path, owner string) error {
	u, err := user.Lookup(owner)
	if err != nil {
		var unk user.UnknownUserError
		if errors.As(err, &unk) {
			return nil // service user not installed yet; keep root-owned
		}
		return err
	}
	uid, err1 := strconv.Atoi(u.Uid)
	gid, err2 := strconv.Atoi(u.Gid)
	if err1 != nil || err2 != nil {
		return nil // non-numeric ids (Windows)
	}
	return os.Chown(path, uid, gid)
}
