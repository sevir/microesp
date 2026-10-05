package link

// SAFETY: the scripts run here are harmless shell commands (sleep, exit).

import (
	"bytes"
	"context"
	"errors"
	"log/slog"
	"runtime"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/microesp/agent/internal/link/dongletest"
	"github.com/microesp/agent/internal/proto"
	"github.com/microesp/agent/internal/scripts"
)

func TestScriptsAnnouncedAfterReady(t *testing.T) {
	h := newHarness(t, dongletest.Normal)
	h.start(t)
	d := h.ready(t)
	waitFor(t, "empty scripts", func() bool { return len(received[*proto.Scripts](d)) == 1 })
	if l := received[*proto.Scripts](d)[0].List; l == nil || len(l) != 0 {
		t.Fatalf("list %+v", l)
	}
	// Order: scripts goes before any telemetry of the session.
	for _, m := range d.Received() {
		if m.Type() == proto.TypeTele {
			t.Fatal("tele sent before scripts")
		}
		if m.Type() == proto.TypeScripts {
			break
		}
	}

	h2 := newHarness(t, dongletest.Normal)
	h2.agent.Scripts = scripts.New([]scripts.Spec{{ID: "backup", Label: "Backup NAS", Command: "true", Timeout: time.Second}}, true, quiet)
	h2.start(t)
	d2 := h2.ready(t)
	waitFor(t, "scripts", func() bool { return len(received[*proto.Scripts](d2)) == 1 })
	if l := received[*proto.Scripts](d2)[0].List; len(l) != 1 || l[0] != (proto.ScriptInfo{ID: "backup", Label: "Backup NAS"}) {
		t.Fatalf("list %+v", l)
	}
	// Sent again on every new session.
	d2.Close()
	waitFor(t, "second session scripts", func() bool {
		return h2.plug.count() >= 2 && len(received[*proto.Scripts](h2.plug.last())) == 1
	})
}

type syncBuf struct {
	mu sync.Mutex
	b  bytes.Buffer
}

func (s *syncBuf) Write(p []byte) (int, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.b.Write(p)
}

func (s *syncBuf) String() string {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.b.String()
}

func TestOldFirmwareRejectsScripts(t *testing.T) {
	h := newHarness(t, dongletest.NoScripts)
	var logs syncBuf
	h.agent.Log = slog.New(slog.NewTextHandler(&logs, nil))
	h.start(t)
	d := h.ready(t)
	waitFor(t, "heartbeat", func() bool { return len(received[*proto.HB](d)) >= 2 })
	_ = d.Send(d.SignedCmd(1, proto.ActionReboot))
	waitFor(t, "exec", func() bool { return len(h.exec.Actions()) == 1 })
	if h.plug.count() != 1 || !d.Ready() {
		t.Fatal("bad_msg to scripts broke the session")
	}
	if out := logs.String(); !strings.Contains(out, "firmware without user scripts support") || strings.Contains(out, "dongle reported error") {
		t.Fatalf("log:\n%s", out)
	}
}

func TestScriptCommands(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("uses sh")
	}
	h := newHarness(t, dongletest.Normal)
	done := make(chan scripts.Result, 4)
	r := scripts.New([]scripts.Spec{{ID: "slow", Label: "Slow", Command: "sleep 0.5; exit 3", Timeout: 5 * time.Second}}, false, quiet)
	r.OnDone = func(res scripts.Result) { done <- res }
	h.agent.Scripts = r
	h.start(t)
	d := h.ready(t)
	ackFor := func(id uint32) proto.Ack {
		t.Helper()
		var a proto.Ack
		waitFor(t, "ack", func() bool { var ok bool; a, ok = d.Ack(id); return ok })
		return a
	}

	// Unknown script id (well formed) -> unknown_action, id not consumed.
	_ = d.Send(d.SignedCmd(1, proto.ScriptAction("nope")))
	if a := ackFor(1); a.OK || a.Err != proto.AckUnknownAction {
		t.Fatalf("unknown ack %+v", a)
	}
	// Malformed id -> unknown_action too.
	_ = d.Send(d.SignedCmd(2, "script:BAD"))
	if a := ackFor(2); a.OK || a.Err != proto.AckUnknownAction {
		t.Fatalf("malformed ack %+v", a)
	}
	// Bad signature is checked first.
	_ = d.Send(&proto.Cmd{ID: 3, Action: "script:slow", Sig: d.SignedCmd(4, "script:slow").Sig})
	if a := ackFor(3); a.Err != proto.AckBadSig {
		t.Fatalf("bad sig ack %+v", a)
	}
	// Valid: ack ok, runs in the background.
	_ = d.Send(d.SignedCmd(4, "script:slow"))
	start := time.Now()
	if a := ackFor(4); !a.OK {
		t.Fatalf("ack %+v", a)
	}
	waitFor(t, "running", func() bool { return r.Running("slow") })
	// Same script again while running -> exec_failed.
	_ = d.Send(d.SignedCmd(5, "script:slow"))
	if a := ackFor(5); a.OK || a.Err != proto.AckExecFailed {
		t.Fatalf("busy ack %+v", a)
	}
	// The session keeps going meanwhile: a power command is executed.
	_ = d.Send(d.SignedCmd(6, proto.ActionReboot))
	waitFor(t, "exec", func() bool { return len(h.exec.Actions()) == 1 })
	if time.Since(start) > 400*time.Millisecond {
		t.Fatal("session blocked by the running script")
	}
	n := len(received[*proto.HB](d))
	waitFor(t, "heartbeat while running", func() bool { return len(received[*proto.HB](d)) > n })
	select {
	case res := <-done:
		if res.ID != "slow" || res.ExitCode != 3 {
			t.Fatalf("%+v", res)
		}
	case <-time.After(3 * time.Second):
		t.Fatal("script did not finish")
	}
	// Replay of a consumed id after exec_failed.
	_ = d.Send(d.SignedCmd(5, "script:slow"))
	waitFor(t, "replay", func() bool { return len(received[*proto.Ack](d)) == 7 })
	if a := received[*proto.Ack](d)[6]; a.ID != 5 || a.Err != proto.AckReplay {
		t.Fatalf("replay ack %+v", a)
	}
}

type fakeRemote struct {
	mu    sync.Mutex
	calls []string
	err   error
	ackAt func() int // acks the dongle had when Start was called
	acks  []int
}

func (f *fakeRemote) Start(_ context.Context, id string) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.calls = append(f.calls, id)
	if f.ackAt != nil {
		f.acks = append(f.acks, f.ackAt())
	}
	return f.err
}

func (f *fakeRemote) set(err error) {
	f.mu.Lock()
	f.err = err
	f.mu.Unlock()
}

func (f *fakeRemote) Calls() []string {
	f.mu.Lock()
	defer f.mu.Unlock()
	return append([]string(nil), f.calls...)
}

func TestRemoteScriptCommands(t *testing.T) {
	h := newHarness(t, dongletest.Normal)
	h.agent.Scripts = scripts.New([]scripts.Spec{{ID: "backup", Label: "Backup", Command: "never-run-locally", Timeout: time.Second}}, false, quiet)
	fr := &fakeRemote{}
	h.agent.ScriptsRemote = fr
	h.start(t)
	d := h.ready(t)
	fr.mu.Lock()
	fr.ackAt = func() int { return len(received[*proto.Ack](d)) }
	fr.mu.Unlock()
	ackFor := func(id uint32) proto.Ack {
		t.Helper()
		var a proto.Ack
		waitFor(t, "ack", func() bool { var ok bool; a, ok = d.Ack(id); return ok })
		return a
	}

	// Not in the agent's own list: rejected without asking the runner.
	_ = d.Send(d.SignedCmd(1, proto.ScriptAction("other")))
	if a := ackFor(1); a.Err != proto.AckUnknownAction || len(fr.Calls()) != 0 {
		t.Fatalf("unknown ack %+v calls %v", a, fr.Calls())
	}
	// Runner accepts: asked before the ack, then ack ok.
	_ = d.Send(d.SignedCmd(2, proto.ScriptAction("backup")))
	if a := ackFor(2); !a.OK {
		t.Fatalf("ok ack %+v", a)
	}
	fr.mu.Lock()
	if len(fr.acks) != 1 || fr.acks[0] != 1 {
		t.Errorf("runner asked after the ack: %v", fr.acks)
	}
	fr.mu.Unlock()
	// A slice, not a map: ids must go out in increasing order or the agent
	// rightly answers replay.
	for _, c := range []struct {
		id   uint32
		err  error
		want string
	}{
		{3, scripts.ErrUnknown, proto.AckUnknownAction},
		{4, scripts.ErrBusy, proto.AckExecFailed},
		{5, errors.New("dial unix /run/microesp/scripts.sock: connect: no such file"), proto.AckExecFailed},
	} {
		fr.set(c.err)
		_ = d.Send(d.SignedCmd(c.id, proto.ScriptAction("backup")))
		if a := ackFor(c.id); a.OK || a.Err != c.want {
			t.Fatalf("id %d ack %+v, want %s", c.id, a, c.want)
		}
	}
	if n := len(fr.Calls()); n != 4 || h.plug.count() != 1 {
		t.Fatalf("calls %d, sessions %d", n, h.plug.count())
	}
}

// With a real runner socket that is unreachable the ack is exec_failed
// within the client timeout.
func TestRemoteScriptUnreachable(t *testing.T) {
	h := newHarness(t, dongletest.Normal)
	h.agent.Scripts = scripts.New([]scripts.Spec{{ID: "backup", Label: "Backup", Command: "x", Timeout: time.Second}}, false, quiet)
	h.agent.ScriptsRemote = &scripts.Client{Path: t.TempDir() + "/missing.sock", Timeout: 200 * time.Millisecond}
	h.start(t)
	d := h.ready(t)
	_ = d.Send(d.SignedCmd(1, proto.ScriptAction("backup")))
	waitFor(t, "ack", func() bool { _, ok := d.Ack(1); return ok })
	if a, _ := d.Ack(1); a.OK || a.Err != proto.AckExecFailed {
		t.Fatalf("ack %+v", a)
	}
}
