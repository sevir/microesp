package config

import (
	"bytes"
	"errors"
	"os"
	"path/filepath"
	"runtime"
	"testing"
)

func TestKeyRoundTrip(t *testing.T) {
	p := filepath.Join(t.TempDir(), "sub", "agent.key")
	key := bytes.Repeat([]byte{0xab}, 32)
	if err := SaveKey(p, key, "microesp-nonexistent-user"); err != nil {
		t.Fatal(err)
	}
	fi, err := os.Stat(p)
	if err != nil {
		t.Fatal(err)
	}
	if runtime.GOOS != "windows" && fi.Mode().Perm() != 0o600 {
		t.Fatalf("mode %o", fi.Mode().Perm())
	}
	got, err := LoadKey(p)
	if err != nil || !bytes.Equal(got, key) {
		t.Fatal(got, err)
	}
	// Overwrite (re-pairing) replaces the key.
	key2 := bytes.Repeat([]byte{0x01}, 32)
	if err := SaveKey(p, key2, ""); err != nil {
		t.Fatal(err)
	}
	if got, _ := LoadKey(p); !bytes.Equal(got, key2) {
		t.Fatal("key not replaced")
	}
	if entries, _ := os.ReadDir(filepath.Dir(p)); len(entries) != 1 {
		t.Errorf("temp files left: %v", entries)
	}
}

func TestKeyErrors(t *testing.T) {
	dir := t.TempDir()
	if err := SaveKey(filepath.Join(dir, "k"), []byte{1}, ""); err == nil {
		t.Error("short key saved")
	}
	if _, err := LoadKey(filepath.Join(dir, "missing")); err == nil {
		t.Error("missing key loaded")
	}
	bad := filepath.Join(dir, "bad")
	_ = os.WriteFile(bad, []byte("zz\n"), 0o600)
	if _, err := LoadKey(bad); err == nil {
		t.Error("bad hex accepted")
	}
	if runtime.GOOS != "windows" {
		open := filepath.Join(dir, "open")
		_ = os.WriteFile(open, bytes.Repeat([]byte("ab"), 32), 0o644)
		_ = os.Chmod(open, 0o644)
		if _, err := LoadKey(open); !errors.Is(err, ErrInsecureKeyFile) {
			t.Errorf("world-readable key: %v", err)
		}
	}
	if err := chownTo(filepath.Join(dir, "x"), "microesp-nonexistent-user"); err != nil {
		t.Error(err)
	}
	if runtime.GOOS != "windows" && os.Geteuid() != 0 {
		// Chown to another existing user must fail for non-root.
		f := filepath.Join(dir, "own")
		_ = os.WriteFile(f, nil, 0o600)
		if err := chownTo(f, "root"); err == nil {
			t.Error("chown to root succeeded as non-root")
		}
	}
}
