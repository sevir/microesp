//go:build linux

package scripts

import (
	"context"
	"errors"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

type testServer struct {
	path   string
	runner *Runner
	done   chan Result
	cancel context.CancelFunc
	exited chan error
}

func startServer(t *testing.T, allow func(uint32) bool) *testServer {
	t.Helper()
	path := filepath.Join(t.TempDir(), "scripts.sock")
	ln, activated, err := Listen(path)
	if err != nil || activated {
		t.Fatalf("listen: %v activated=%v", err, activated)
	}
	if fi, err := os.Stat(path); err != nil || fi.Mode().Perm() != 0o660 {
		t.Fatalf("socket mode: %v %v", fi.Mode(), err)
	}
	r := New([]Spec{
		{ID: "slow", Label: "Slow", Command: "sleep 0.4", Timeout: 5 * time.Second},
		{ID: "fast", Label: "Fast", Command: "exit 0", Timeout: 5 * time.Second},
	}, false, quiet)
	ts := &testServer{path: path, runner: r, done: make(chan Result, 8), exited: make(chan error, 1)}
	r.OnDone = func(res Result) { ts.done <- res }
	ctx, cancel := context.WithCancel(context.Background())
	ts.cancel = cancel
	srv := &Server{Runner: r, Log: quiet, AllowUID: allow}
	go func() { ts.exited <- srv.Serve(ctx, ln) }()
	t.Cleanup(func() {
		cancel()
		if err := <-ts.exited; err != nil {
			t.Errorf("Serve: %v", err)
		}
	})
	return ts
}

func allowAll(uint32) bool { return true }

func raw(t *testing.T, path, line string) string {
	t.Helper()
	c, err := net.Dial("unix", path)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	_ = c.SetDeadline(time.Now().Add(3 * time.Second))
	if _, err := c.Write([]byte(line)); err != nil {
		return "write: " + err.Error()
	}
	b, _ := readLine(c)
	return strings.TrimSpace(string(b))
}

func TestServerProtocol(t *testing.T) {
	ts := startServer(t, allowAll)
	c := &Client{Path: ts.path}
	ctx := context.Background()

	if err := c.Start(ctx, "slow"); err != nil {
		t.Fatalf("slow: %v", err)
	}
	if err := c.Start(ctx, "slow"); !errors.Is(err, ErrBusy) {
		t.Fatalf("busy: %v", err)
	}
	if err := c.Start(ctx, "nope"); !errors.Is(err, ErrUnknown) {
		t.Fatalf("unknown: %v", err)
	}
	if err := c.Start(ctx, "BAD ID"); !errors.Is(err, ErrBadRequest) {
		t.Fatalf("bad id: %v", err)
	}
	if got := raw(t, ts.path, "not json\n"); got != `{"ok":false,"err":"bad_request"}` {
		t.Fatalf("not json: %s", got)
	}
	if got := raw(t, ts.path, `{"id":"`+strings.Repeat("a", 300)+"\"}\n"); got != `{"ok":false,"err":"bad_request"}` {
		t.Fatalf("oversized: %s", got)
	}
	if got := raw(t, ts.path, "{\"id\":\"fast\"}\n"); got != `{"ok":true}` {
		t.Fatalf("raw ok: %s", got)
	}
	got := map[string]bool{}
	for len(got) < 2 {
		select {
		case res := <-ts.done:
			got[res.ID] = res.Err == nil
		case <-time.After(3 * time.Second):
			t.Fatalf("scripts did not finish: %v", got)
		}
	}
	if !got["slow"] || !got["fast"] {
		t.Fatalf("%v", got)
	}
}

func TestServerRejectsPeer(t *testing.T) {
	seenc := make(chan uint32, 4)
	ts := startServer(t, func(uid uint32) bool { seenc <- uid; return false })
	err := (&Client{Path: ts.path}).Start(context.Background(), "fast")
	if err == nil || errors.Is(err, ErrUnknown) || errors.Is(err, ErrBusy) {
		t.Fatalf("rejected peer got %v", err)
	}
	if uid := <-seenc; uid != uint32(os.Getuid()) || len(seenc) != 0 {
		t.Fatalf("peer uid %d, want %d", uid, os.Getuid())
	}
	if ts.runner.Running("fast") {
		t.Fatal("script started for a rejected peer")
	}
}

func TestAllowAgent(t *testing.T) {
	f := AllowAgent(998, true)
	if !f(0) || !f(998) || f(1000) {
		t.Error("with agent uid")
	}
	g := AllowAgent(0, false)
	if !g(0) || g(998) {
		t.Error("without agent user")
	}
}

func TestClientUnreachableAndTimeout(t *testing.T) {
	dir := t.TempDir()
	if err := (&Client{Path: filepath.Join(dir, "missing.sock")}).Start(context.Background(), "x"); err == nil || !strings.Contains(err.Error(), "unreachable") {
		t.Fatalf("missing socket: %v", err)
	}
	// A listener that never answers -> timeout.
	path := filepath.Join(dir, "mute.sock")
	ln, err := net.Listen("unix", path)
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	go func() {
		for {
			c, err := ln.Accept()
			if err != nil {
				return
			}
			defer c.Close()
		}
	}()
	start := time.Now()
	if err := (&Client{Path: path, Timeout: 100 * time.Millisecond}).Start(context.Background(), "x"); err == nil {
		t.Fatal("mute runner accepted")
	}
	if time.Since(start) > time.Second {
		t.Fatal("client did not time out")
	}
}

func TestActivationFD(t *testing.T) {
	env := func(m map[string]string) func(string) string { return func(k string) string { return m[k] } }
	if _, ok, err := activationFD(env(nil), 42); ok || err != nil {
		t.Fatalf("not activated: %v %v", ok, err)
	}
	if fd, ok, err := activationFD(env(map[string]string{"LISTEN_PID": "42", "LISTEN_FDS": "1"}), 42); fd != 3 || !ok || err != nil {
		t.Fatalf("activated: %d %v %v", fd, ok, err)
	}
	for _, m := range []map[string]string{
		{"LISTEN_PID": "41", "LISTEN_FDS": "1"},
		{"LISTEN_PID": "x", "LISTEN_FDS": "1"},
		{"LISTEN_PID": "42", "LISTEN_FDS": "0"},
		{"LISTEN_PID": "42", "LISTEN_FDS": "2"},
		{"LISTEN_FDS": "1"},
	} {
		if _, ok, err := activationFD(env(m), 42); ok || err == nil {
			t.Errorf("%v accepted", m)
		}
	}
}

func TestListenerFromFD(t *testing.T) {
	path := filepath.Join(t.TempDir(), "act.sock")
	ln, err := net.Listen("unix", path)
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()
	f, err := ln.(*net.UnixListener).File() // a dup, as systemd would pass
	if err != nil {
		t.Fatal(err)
	}
	al, err := listenerFromFD(int(f.Fd()))
	if err != nil {
		t.Fatal(err)
	}
	defer al.Close()
	go func() {
		c, err := al.Accept()
		if err == nil {
			_, _ = c.Write([]byte("hi\n"))
			c.Close()
		}
	}()
	c, err := net.Dial("unix", path)
	if err != nil {
		t.Fatal(err)
	}
	defer c.Close()
	_ = c.SetDeadline(time.Now().Add(3 * time.Second))
	if b, _ := readLine(c); string(b) != "hi\n" {
		t.Fatalf("got %q", b)
	}
	// Activation env for another pid is an error for Listen.
	t.Setenv("LISTEN_PID", "1")
	t.Setenv("LISTEN_FDS", "1")
	if _, _, err := Listen(""); err == nil {
		t.Fatal("foreign LISTEN_PID accepted")
	}
	t.Setenv("LISTEN_PID", "")
	t.Setenv("LISTEN_FDS", "")
	if _, _, err := Listen(""); err == nil {
		t.Fatal("no path accepted")
	}
}
