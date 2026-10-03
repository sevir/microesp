package link

// SAFETY: all tests use in-memory pipes (net.Pipe) and a recording fake
// executor. No serial device is opened and no power action is executed.

import (
	"bytes"
	"context"
	"encoding/hex"
	"errors"
	"io"
	"log/slog"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/microesp/agent/internal/link/dongletest"
	"github.com/microesp/agent/internal/proto"
	"github.com/microesp/agent/internal/telemetry"
)

var (
	quiet   = slog.New(slog.NewTextHandler(io.Discard, nil))
	testKey = mustHex("1a04cfbd6d99e3b3bfe48a078b42529690c08951f6d23ec1efe6d06440f2fb03")
)

func mustHex(s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		panic(err)
	}
	return b
}

func waitFor(t *testing.T, what string, cond func() bool) {
	t.Helper()
	deadline := time.Now().Add(3 * time.Second)
	for !cond() {
		if time.Now().After(deadline) {
			t.Fatalf("timeout waiting for %s", what)
		}
		time.Sleep(2 * time.Millisecond)
	}
}

// recExec records actions and checks that the matching ack{ok:true}
// reached the dongle before Execute was called.
type recExec struct {
	mu        sync.Mutex
	dongle    func() *dongletest.Dongle
	actions   []string
	notes     []string
	ackFirst  []bool
	preflight error
}

func (e *recExec) Name() string { return "fake" }

func (e *recExec) okAcks() int {
	n := 0
	for _, m := range e.dongle().Received() {
		if a, ok := m.(*proto.Ack); ok && a.OK {
			n++
		}
	}
	return n
}

func (e *recExec) Execute(_ context.Context, action string) error {
	e.mu.Lock()
	want := len(e.actions) + 1
	e.mu.Unlock()
	deadline := time.Now().Add(time.Second)
	seen := false
	for time.Now().Before(deadline) {
		if e.okAcks() >= want {
			seen = true
			break
		}
		time.Sleep(time.Millisecond)
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	e.actions = append(e.actions, action)
	e.ackFirst = append(e.ackFirst, seen)
	return nil
}

func (e *recExec) Notify(_ context.Context, msg string) {
	e.mu.Lock()
	e.notes = append(e.notes, msg)
	e.mu.Unlock()
}

func (e *recExec) Actions() []string {
	e.mu.Lock()
	defer e.mu.Unlock()
	return append([]string(nil), e.actions...)
}

func (e *recExec) Notes() []string {
	e.mu.Lock()
	defer e.mu.Unlock()
	return append([]string(nil), e.notes...)
}

type preflightExec struct{ *recExec }

func (p preflightExec) Preflight() error { return p.preflight }

// plug hands out a fresh fake dongle on each Open.
type plug struct {
	mu      sync.Mutex
	mode    dongletest.Mode
	dongles []*dongletest.Dongle
}

func (p *plug) open(string) (io.ReadWriteCloser, error) {
	p.mu.Lock()
	defer p.mu.Unlock()
	rw, d := dongletest.New(testKey, p.mode)
	p.dongles = append(p.dongles, d)
	return rw, nil
}

func (p *plug) count() int {
	p.mu.Lock()
	defer p.mu.Unlock()
	return len(p.dongles)
}

func (p *plug) last() *dongletest.Dongle {
	p.mu.Lock()
	defer p.mu.Unlock()
	if len(p.dongles) == 0 {
		return nil
	}
	return p.dongles[len(p.dongles)-1]
}

type harness struct {
	agent  *Agent
	plug   *plug
	exec   *recExec
	coll   *telemetry.Fake
	cancel context.CancelFunc
	done   chan error
}

func newHarness(t *testing.T, mode dongletest.Mode) *harness {
	t.Helper()
	p := &plug{mode: mode}
	ex := &recExec{dongle: p.last}
	coll := &telemetry.Fake{}
	coll.Set(telemetry.Snapshot{CPU: 123, Mem: 456, DiskFree: 789, Uptime: 3600}, nil)
	a := &Agent{
		Device:  "/dev/fake-microesp",
		Open:    p.open,
		LoadKey: func() ([]byte, error) { return testKey, nil },
		HostInfo: func() (telemetry.HostInfo, error) {
			return telemetry.HostInfo{Hostname: "thinkstation", MACs: []string{"fc:9d:05:18:ee:32"}}, nil
		},
		Collector:         coll,
		Executor:          ex,
		Version:           "1.0.0-test",
		OS:                "linux",
		TelemetryInterval: 300 * time.Millisecond,
		HeartbeatInterval: 40 * time.Millisecond,
		HandshakeTimeout:  300 * time.Millisecond,
		MinBackoff:        10 * time.Millisecond,
		MaxBackoff:        40 * time.Millisecond,
		NotFoundPoll:      10 * time.Millisecond,
		Log:               quiet,
	}
	return &harness{agent: a, plug: p, exec: ex, coll: coll}
}

func (h *harness) start(t *testing.T) {
	t.Helper()
	ctx, cancel := context.WithCancel(context.Background())
	h.cancel = cancel
	h.done = make(chan error, 1)
	go func() { h.done <- h.agent.Run(ctx) }()
	t.Cleanup(h.stop)
}

func (h *harness) stop() {
	if h.cancel == nil {
		return
	}
	h.cancel()
	<-h.done
	h.cancel = nil
}

func (h *harness) ready(t *testing.T) *dongletest.Dongle {
	t.Helper()
	waitFor(t, "session ready", func() bool { d := h.plug.last(); return d != nil && d.Ready() })
	return h.plug.last()
}

func received[T proto.Message](d *dongletest.Dongle) []T {
	var out []T
	for _, m := range d.Received() {
		if x, ok := m.(T); ok {
			out = append(out, x)
		}
	}
	return out
}

func TestSessionTelemetryHeartbeat(t *testing.T) {
	h := newHarness(t, dongletest.Normal)
	h.start(t)
	d := h.ready(t)

	hello := received[*proto.Hello](d)[0]
	if hello.Host != "thinkstation" || hello.OS != "linux" || hello.AgentVer != "1.0.0-test" ||
		len(hello.MACs) != 1 || hello.MACs[0] != "fc:9d:05:18:ee:32" || !proto.ValidNonce(hello.Nonce) {
		t.Fatalf("hello = %+v", hello)
	}
	waitFor(t, "first tele", func() bool { return len(received[*proto.Tele](d)) >= 1 })
	tl := received[*proto.Tele](d)[0]
	if tl.Seq != 1 || tl.CPU != 123 || tl.Mem != 456 || tl.DiskFree != 789 || tl.Uptime != 3600 {
		t.Fatalf("tele = %+v", tl)
	}
	waitFor(t, "heartbeat", func() bool { return len(received[*proto.HB](d)) >= 1 })

	// A change > 20 tenths is sent before the telemetry interval elapses.
	h.coll.Set(telemetry.Snapshot{CPU: 500, Mem: 456, DiskFree: 789, Uptime: 3601}, nil)
	start := time.Now()
	waitFor(t, "tele on change", func() bool {
		ts := received[*proto.Tele](d)
		return len(ts) >= 2 && ts[len(ts)-1].CPU == 500
	})
	if time.Since(start) > h.agent.TelemetryInterval {
		t.Error("change not reported promptly")
	}
	ts := received[*proto.Tele](d)
	for i, x := range ts {
		if x.Seq != uint32(i+1) {
			t.Fatalf("seq not monotonic: %+v", ts)
		}
	}
	// Periodic resend after the interval even without change.
	n := len(ts)
	waitFor(t, "periodic tele", func() bool { return len(received[*proto.Tele](d)) > n })

	// Sample errors are tolerated.
	h.coll.Set(telemetry.Snapshot{}, errors.New("boom"))
	c0 := h.coll.Calls()
	waitFor(t, "more samples", func() bool { return h.coll.Calls() > c0+1 })
	if !d.Ready() {
		t.Fatal("session dropped on sample error")
	}
}

func TestCommandRules(t *testing.T) {
	h := newHarness(t, dongletest.Normal)
	h.start(t)
	d := h.ready(t)
	ackFor := func(id uint32) proto.Ack {
		t.Helper()
		var a proto.Ack
		waitFor(t, "ack", func() bool { var ok bool; a, ok = d.Ack(id); return ok })
		return a
	}
	send := func(m proto.Message) {
		t.Helper()
		if err := d.Send(m); err != nil {
			t.Fatal(err)
		}
	}

	// Valid shutdown: ack ok, then executed.
	send(d.SignedCmd(1, proto.ActionShutdown))
	if a := ackFor(1); !a.OK {
		t.Fatalf("ack %+v", a)
	}
	waitFor(t, "execute", func() bool { return len(h.exec.Actions()) == 1 })

	// Bad signature (vector cmd_bad_sig).
	send(&proto.Cmd{ID: 2, Action: proto.ActionReboot, Sig: strings.Repeat("0", 64)})
	if a := ackFor(2); a.OK || a.Err != proto.AckBadSig {
		t.Fatalf("bad sig ack %+v", a)
	}
	// Replay of id 1 (valid sig) -> replay. Ack map is keyed by id, so
	// look at the last ack received instead.
	send(d.SignedCmd(1, proto.ActionShutdown))
	waitFor(t, "replay ack", func() bool {
		as := received[*proto.Ack](d)
		return len(as) == 3
	})
	if a := received[*proto.Ack](d)[2]; a.ID != 1 || a.OK || a.Err != proto.AckReplay {
		t.Fatalf("replay ack %+v", a)
	}
	// Unknown action, correctly signed.
	send(d.SignedCmd(3, "format_disk"))
	if a := ackFor(3); a.OK || a.Err != proto.AckUnknownAction {
		t.Fatalf("unknown action ack %+v", a)
	}
	// Signature from another session's nonces (vector cmd) is rejected.
	send(&proto.Cmd{ID: 4, Action: proto.ActionShutdown, Sig: "1ee345c5aa9d3ce4bd1b8517449ee526493f0d5b3517f84b05324b82f4f43604"})
	if a := ackFor(4); a.Err != proto.AckBadSig {
		t.Fatalf("foreign-session sig ack %+v", a)
	}
	// Valid reboot with a higher id.
	send(d.SignedCmd(7, proto.ActionReboot))
	if a := ackFor(7); !a.OK {
		t.Fatalf("ack %+v", a)
	}
	waitFor(t, "execute 2", func() bool { return len(h.exec.Actions()) == 2 })
	// Lower id after 7 is a replay even if never seen.
	send(d.SignedCmd(5, proto.ActionReboot))
	if a := ackFor(5); a.Err != proto.AckReplay {
		t.Fatalf("lower id ack %+v", a)
	}

	time.Sleep(50 * time.Millisecond)
	if got := strings.Join(h.exec.Actions(), ","); got != "shutdown,reboot" {
		t.Fatalf("executed %s", got)
	}
	for i, ok := range h.exec.ackFirst {
		if !ok {
			t.Errorf("action %d executed before its ack reached the dongle", i)
		}
	}
}

func TestCommandIDsResetPerSession(t *testing.T) {
	h := newHarness(t, dongletest.Normal)
	h.start(t)
	d := h.ready(t)
	_ = d.Send(d.SignedCmd(1, proto.ActionReboot))
	waitFor(t, "exec", func() bool { return len(h.exec.Actions()) == 1 })
	d.Close()
	waitFor(t, "reconnect", func() bool { return h.plug.count() >= 2 && h.plug.last().Ready() })
	d2 := h.plug.last()
	// The old session's command replayed on the new session fails the sig.
	_ = d2.Send(d.SignedCmd(1, proto.ActionReboot))
	waitFor(t, "ack", func() bool { _, ok := d2.Ack(1); return ok })
	if a, _ := d2.Ack(1); a.Err != proto.AckBadSig {
		t.Fatalf("cross-session replay ack %+v", a)
	}
	// A fresh id 1 on the new session is accepted.
	_ = d2.Send(d2.SignedCmd(1, proto.ActionReboot))
	waitFor(t, "exec 2", func() bool { return len(h.exec.Actions()) == 2 })
}

func TestExecFailedOnPreflight(t *testing.T) {
	h := newHarness(t, dongletest.Normal)
	h.exec.preflight = errors.New("systemctl missing")
	h.agent.Executor = preflightExec{h.exec}
	h.start(t)
	d := h.ready(t)
	_ = d.Send(d.SignedCmd(1, proto.ActionShutdown))
	waitFor(t, "ack", func() bool { _, ok := d.Ack(1); return ok })
	if a, _ := d.Ack(1); a.OK || a.Err != proto.AckExecFailed {
		t.Fatalf("ack %+v", a)
	}
	time.Sleep(30 * time.Millisecond)
	if len(h.exec.Actions()) != 0 {
		t.Fatal("executed despite failed preflight")
	}
}

func TestNoticeAndNoise(t *testing.T) {
	h := newHarness(t, dongletest.Normal)
	h.start(t)
	d := h.ready(t)
	_ = d.Send(&proto.Notice{Action: proto.ActionShutdown, In: 10})
	_ = d.Send(&proto.Notice{Action: proto.ActionReboot, In: 5})
	_ = d.Send(&proto.Notice{Action: proto.ActionCancel, In: 0})
	_ = d.SendRaw([]byte(strings.Repeat("x", 700) + "\n"))
	_ = d.SendRaw([]byte("garbage\n{\"t\":\"nope\"}\n"))
	_ = d.Send(&proto.Err{Code: proto.CodeBadMsg})
	_ = d.Send(&proto.Ready{})
	waitFor(t, "notifications", func() bool { return len(h.exec.Notes()) == 3 })
	// Session survives noise: a command still works.
	_ = d.Send(d.SignedCmd(1, proto.ActionReboot))
	waitFor(t, "exec", func() bool { return len(h.exec.Actions()) == 1 })
	if h.plug.count() != 1 {
		t.Fatal("noise caused a reconnect")
	}
}

func TestReconnectAfterUnplug(t *testing.T) {
	h := newHarness(t, dongletest.Normal)
	h.start(t)
	d := h.ready(t)
	d.Close()
	waitFor(t, "second session", func() bool { return h.plug.count() >= 2 && h.plug.last().Ready() })
}

func TestUnauthInReadyRestartsSession(t *testing.T) {
	h := newHarness(t, dongletest.Normal)
	h.start(t)
	d := h.ready(t)
	_ = d.Send(&proto.Err{Code: proto.CodeUnauth})
	waitFor(t, "new session", func() bool { return h.plug.count() >= 2 && h.plug.last().Ready() })
}

func TestHandshakeFailuresBackoff(t *testing.T) {
	for _, mode := range []dongletest.Mode{dongletest.NotPaired, dongletest.BadWelcomeSig, dongletest.Silent, dongletest.RejectAuth} {
		h := newHarness(t, mode)
		h.agent.defaults()
		_, err := h.agent.runOnce(context.Background())
		switch mode {
		case dongletest.NotPaired:
			if !IsDongleError(err, proto.CodeNotPaired) {
				t.Errorf("not paired: %v", err)
			}
		case dongletest.BadWelcomeSig:
			if !errors.Is(err, ErrBadWelcomeSig) {
				t.Errorf("bad welcome: %v", err)
			}
			// The agent must not have sent auth to an impostor.
			waitFor(t, "dongle closed", func() bool {
				select {
				case <-h.plug.last().Done():
					return true
				default:
					return false
				}
			})
			if len(received[*proto.Auth](h.plug.last())) != 0 {
				t.Error("auth sent to impostor")
			}
		case dongletest.Silent:
			if !errors.Is(err, ErrTimeout) {
				t.Errorf("silent: %v", err)
			}
		case dongletest.RejectAuth:
			if !IsDongleError(err, proto.CodeUnauth) {
				t.Errorf("reject auth: %v", err)
			}
		}
		// Run retries with backoff.
		h.start(t)
		waitFor(t, "retries", func() bool { return h.plug.count() >= 3 })
		h.stop()
	}
}

func TestRunNotFoundAndKeyErrors(t *testing.T) {
	h := newHarness(t, dongletest.Normal)
	h.agent.Device = "auto"
	var calls int
	var mu sync.Mutex
	h.agent.List = func() ([]PortInfo, error) {
		mu.Lock()
		defer mu.Unlock()
		calls++
		if calls < 3 {
			return []PortInfo{{Name: "/dev/ttyUSB0", VID: "10c4", PID: "ea60"}}, nil
		}
		return []PortInfo{{Name: "/dev/ttyACM7", IsUSB: true, VID: "303A", PID: "4002"}}, nil
	}
	var keyCalls int
	h.agent.LoadKey = func() ([]byte, error) {
		mu.Lock()
		defer mu.Unlock()
		keyCalls++
		if keyCalls < 2 {
			return nil, errors.New("no key")
		}
		return testKey, nil
	}
	h.agent.HostInfo = func() (telemetry.HostInfo, error) { return telemetry.HostInfo{Hostname: "h"}, errors.New("partial") }
	h.start(t)
	h.ready(t)
	if received[*proto.Hello](h.plug.last())[0].MACs == nil {
		t.Error("macs must be [] not null")
	}
}

func TestOpenError(t *testing.T) {
	h := newHarness(t, dongletest.Normal)
	h.agent.Open = func(string) (io.ReadWriteCloser, error) { return nil, errors.New("busy") }
	h.agent.defaults()
	if ready, err := h.agent.runOnce(context.Background()); ready || err == nil || !strings.Contains(err.Error(), "busy") {
		t.Fatal(ready, err)
	}
}

func TestNextBackoff(t *testing.T) {
	var seq []time.Duration
	b := time.Duration(0)
	for i := 0; i < 8; i++ {
		b = NextBackoff(b, time.Second, 30*time.Second)
		seq = append(seq, b)
	}
	want := []time.Duration{1, 2, 4, 8, 16, 30, 30, 30}
	for i := range want {
		if seq[i] != want[i]*time.Second {
			t.Fatalf("backoff %v", seq)
		}
	}
}

func TestPairing(t *testing.T) {
	rw, d := dongletest.New(nil, dongletest.Normal)
	d.PairCode = "482913"
	c := NewConn(rw, quiet)
	defer c.Close()
	key, err := Pair(context.Background(), c, "482913", time.Second)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(key, d.PairedKey()) || len(key) != 32 {
		t.Fatal("key mismatch")
	}
	// The new key authenticates a session.
	hs, err := Handshake(context.Background(), c, key, HelloParams{Host: "h", OS: "windows", AgentVer: "1"}, time.Second)
	if err != nil || hs.Welcome.Dev != "907069f662dc" {
		t.Fatal(hs, err)
	}

	// Wrong code -> pair_failed, nothing returned.
	if _, err := Pair(context.Background(), c, "000000", time.Second); !IsDongleError(err, proto.CodePairFailed) {
		t.Fatalf("wrong code: %v", err)
	}
	if _, err := Pair(context.Background(), c, "12ab56", time.Second); err == nil {
		t.Fatal("invalid code accepted")
	}
}

func TestPairBadOKSig(t *testing.T) {
	a, b := pipe()
	c := NewConn(a, quiet)
	defer c.Close()
	go func() {
		lr := proto.NewLineReader(b)
		_, _ = lr.ReadLine()
		writeMsg(b, &proto.PairChal{Nonce: "0f1e2d3c4b5a6978"})
		_, _ = lr.ReadLine()
		writeMsg(b, &proto.PairOK{Sig: strings.Repeat("a", 64)})
	}()
	if _, err := Pair(context.Background(), c, "482913", time.Second); !errors.Is(err, ErrBadPairOKSig) {
		t.Fatal(err)
	}
}

func TestConnEdgeCases(t *testing.T) {
	a, b := pipe()
	c := NewConn(a, nil)
	// Closed peer: send fails, Expect returns the read error.
	_ = b.Close()
	if err := c.Send(&proto.HB{}); err == nil {
		t.Error("send on closed pipe")
	}
	if _, err := c.Expect(context.Background(), time.Second, func(proto.Message) bool { return true }); err == nil {
		t.Error("expect on closed pipe")
	}
	_ = c.Close()
	_ = c.Close() // idempotent

	// Context cancellation.
	a, b = pipe()
	defer b.Close()
	c = NewConn(a, quiet)
	defer c.Close()
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, err := c.Expect(ctx, time.Second, func(proto.Message) bool { return true }); !errors.Is(err, context.Canceled) {
		t.Error(err)
	}
	// Encoding failure.
	if err := c.Send(&proto.Hello{Host: strings.Repeat("x", 600)}); err == nil {
		t.Error("oversized message sent")
	}
	if (&DongleError{Code: "x"}).Error() != "dongle error: x" {
		t.Error()
	}
}

func TestWriteTimeout(t *testing.T) {
	old := WriteTimeout
	WriteTimeout = 20 * time.Millisecond
	defer func() { WriteTimeout = old }()
	a, b := pipe() // nobody reads b
	defer b.Close()
	c := NewConn(a, quiet)
	defer c.Close()
	if err := c.Send(&proto.HB{}); err == nil || !strings.Contains(err.Error(), "timeout") {
		t.Fatal(err)
	}
}

func TestDiscovery(t *testing.T) {
	list := func() ([]PortInfo, error) {
		return []PortInfo{
			{Name: "/dev/ttyS0"},
			{Name: "/dev/ttyACM1", IsUSB: true, VID: "303a", PID: "1001"},                             // ESP32-S3 USB-Serial/JTAG
			{Name: "/dev/ttyACM3", IsUSB: true, VID: "1234", PID: "0001", Product: "MicroESP Dongle"}, // foreign VID: rejected
			{Name: "/dev/ttyACM4", IsUSB: true, VID: "303a", PID: "9999", Product: "MicroESP"},
			{Name: "/dev/ttyACM2", IsUSB: true, VID: "303A", PID: "4002"},
		}, nil
	}
	cs, err := Candidates(list)
	if err != nil || len(cs) != 2 || cs[0].Name != "/dev/ttyACM2" {
		t.Fatalf("%+v %v", cs, err)
	}
	if p, _ := Find(list, "auto"); p != "/dev/ttyACM2" {
		t.Error(p)
	}
	if p, _ := Find(list, "/dev/microesp"); p != "/dev/microesp" {
		t.Error(p)
	}
	if _, err := Find(func() ([]PortInfo, error) { return nil, nil }, ""); !errors.Is(err, ErrNotFound) {
		t.Error(err)
	}
	if _, err := Find(func() ([]PortInfo, error) { return nil, errors.New("x") }, "auto"); err == nil {
		t.Error("lister error swallowed")
	}
}

func TestSerialOpenerMissing(t *testing.T) {
	// SerialLister is deliberately not called: it may probe ttyS* ports.
	if _, err := SerialOpener("/nonexistent/microesp-test"); err == nil {
		t.Error("open of missing path succeeded")
	}
}

// Regression: with a tick of HeartbeatInterval/2, sampling and heartbeat
// slipped to 1.5x their period (telemetry every 15 s instead of 10 s,
// hb every 7.5 s instead of 5 s).
func TestTelemetryAndHeartbeatCadence(t *testing.T) {
	h := newHarness(t, dongletest.Normal)
	h.agent.HeartbeatInterval = 200 * time.Millisecond
	h.agent.TelemetryInterval = 600 * time.Millisecond
	h.start(t)
	d := h.ready(t)
	waitFor(t, "first tele", func() bool { return len(received[*proto.Tele](d)) >= 1 })
	t0, h0 := len(received[*proto.Tele](d)), len(received[*proto.HB](d))
	time.Sleep(2450 * time.Millisecond)
	tele := len(received[*proto.Tele](d)) - t0
	hb := len(received[*proto.HB](d)) - h0
	// Expected: 4 tele (every 600 ms) and ~8 hb (every 200 ms between teles).
	if tele < 3 {
		t.Errorf("only %d periodic tele in 2.45 s with a 600 ms interval", tele)
	}
	if tele+hb < 10 {
		t.Errorf("only %d messages (tele %d, hb %d) in 2.45 s with a 200 ms heartbeat", tele+hb, tele, hb)
	}
}
