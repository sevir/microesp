//go:build linux

package main

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/microesp/agent/internal/scripts"
)

// The script run here only touches a file in the test's temp dir.
func TestScriptsRunnerSubcommand(t *testing.T) {
	e := newEnv(t)
	marker := filepath.Join(e.dir, "ran")
	sock := filepath.Join(e.dir, "scripts.sock")
	body := "log_level = \"error\"\nscripts_socket = \"" + sock + "\"\n\n[[scripts]]\nid = \"touch\"\nlabel = \"Touch\"\ncommand = \"touch '" + marker + "'\"\n"
	if err := os.WriteFile(e.cfg, []byte(body), 0o600); err != nil {
		t.Fatal(err)
	}
	// status shows the runner mode (no dry_run in this config; status runs nothing).
	if rc := runMain([]string{"status", "--config", e.cfg}, e.deps()); rc != 1 && rc != 0 {
		t.Fatalf("status rc=%d", rc)
	}
	if !strings.Contains(e.out.String(), "scripts:                 1 [touch] via runner "+sock) {
		t.Fatalf("status: %s", e.out.String())
	}

	old := lookupUID
	lookupUID = func(string) (uint32, bool) { return uint32(os.Getuid()), true }
	t.Cleanup(func() { lookupUID = old })

	done := make(chan int, 1)
	go func() { done <- runMain([]string{"scripts-runner", "--config", e.cfg, "--listen", sock}, e.deps()) }()
	c := &scripts.Client{Path: sock}
	waitFor(t, "runner socket", func() bool { return dialable(sock) })
	if err := c.Start(context.Background(), "touch"); err != nil {
		t.Fatalf("start: %v (%s)", err, e.errb.String())
	}
	if err := c.Start(context.Background(), "nope"); !errors.Is(err, scripts.ErrUnknown) {
		t.Fatalf("unknown: %v", err)
	}
	waitFor(t, "script ran", func() bool { _, err := os.Stat(marker); return err == nil })
	e.cancel()
	select {
	case rc := <-done:
		if rc != 0 {
			t.Fatalf("runner rc=%d %s", rc, e.errb.String())
		}
	case <-time.After(5 * time.Second):
		t.Fatal("runner did not stop")
	}

	// Without the agent user the runner refuses our uid.
	lookupUID = func(string) (uint32, bool) { return 0, false }
	e.ctx, e.cancel = context.WithCancel(context.Background())
	go func() { done <- runMain([]string{"scripts-runner", "--config", e.cfg, "--listen", sock}, e.deps()) }()
	waitFor(t, "runner socket", func() bool { return dialable(sock) })
	if err := c.Start(context.Background(), "touch"); err == nil || errors.Is(err, scripts.ErrUnknown) {
		t.Fatalf("foreign uid accepted: %v", err)
	}
	e.cancel()
	<-done

	// No socket and no activation: error.
	if rc := runMain([]string{"scripts-runner", "--config", e.cfg}, e.deps()); rc != 1 {
		t.Fatalf("rc=%d", rc)
	}
}

func dialable(path string) bool {
	_, err := os.Stat(path)
	return err == nil
}
