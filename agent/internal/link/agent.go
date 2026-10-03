package link

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"time"

	"github.com/microesp/agent/internal/power"
	"github.com/microesp/agent/internal/proto"
	"github.com/microesp/agent/internal/telemetry"
)

// Agent maintains the session with the dongle: discovery, handshake,
// telemetry, heartbeat, command handling and reconnection.
type Agent struct {
	Device    string // path or "auto"
	List      Lister
	Open      Opener
	LoadKey   func() ([]byte, error)
	HostInfo  func() (telemetry.HostInfo, error)
	Collector telemetry.Collector
	Executor  power.Executor
	Version   string
	OS        string

	TelemetryInterval time.Duration // default 10s
	HeartbeatInterval time.Duration // default 5s; also the CPU sampling window
	HandshakeTimeout  time.Duration // default 3s
	MinBackoff        time.Duration // default 1s
	MaxBackoff        time.Duration // default 30s
	NotFoundPoll      time.Duration // default 2s
	ExecTimeout       time.Duration // default 30s

	Log *slog.Logger

	// OnReady is called after each successful handshake (tests, status).
	OnReady func(*Session)
}

func (a *Agent) defaults() {
	set := func(d *time.Duration, v time.Duration) {
		if *d <= 0 {
			*d = v
		}
	}
	set(&a.TelemetryInterval, 10*time.Second)
	set(&a.HeartbeatInterval, 5*time.Second)
	set(&a.HandshakeTimeout, HandshakeTimeout)
	set(&a.MinBackoff, time.Second)
	set(&a.MaxBackoff, 30*time.Second)
	set(&a.NotFoundPoll, 2*time.Second)
	set(&a.ExecTimeout, 30*time.Second)
	if a.Log == nil {
		a.Log = slog.Default()
	}
	if a.List == nil {
		a.List = SerialLister
	}
	if a.Open == nil {
		a.Open = SerialOpener
	}
}

// NextBackoff doubles cur within [min, max].
func NextBackoff(cur, min, max time.Duration) time.Duration {
	if cur < min {
		return min
	}
	if cur*2 > max {
		return max
	}
	return cur * 2
}

// Run keeps a session alive until ctx is cancelled. It returns nil on
// cancellation; all other failures are retried.
func (a *Agent) Run(ctx context.Context) error {
	a.defaults()
	backoff := a.MinBackoff
	for {
		ready, err := a.runOnce(ctx)
		if ctx.Err() != nil {
			return nil
		}
		var wait time.Duration
		switch {
		case errors.Is(err, ErrNotFound):
			// Cheap polling so a re-plugged dongle is picked up quickly;
			// does not escalate the handshake backoff.
			wait = a.NotFoundPoll
			a.Log.Debug("dongle not found", "retry_in", wait)
		case ready:
			backoff = a.MinBackoff
			wait = backoff
			a.Log.Warn("session lost", "err", err, "retry_in", wait)
		default:
			wait = backoff
			backoff = NextBackoff(backoff, a.MinBackoff, a.MaxBackoff)
			a.Log.Warn("connection attempt failed", "err", err, "retry_in", wait)
		}
		t := time.NewTimer(wait)
		select {
		case <-ctx.Done():
			t.Stop()
			return nil
		case <-t.C:
		}
	}
}

// runOnce performs one connection: returns whether ready was reached.
func (a *Agent) runOnce(ctx context.Context) (bool, error) {
	path, err := Find(a.List, a.Device)
	if err != nil {
		return false, err
	}
	key, err := a.LoadKey()
	if err != nil {
		return false, fmt.Errorf("load key (run `microesp-agent pair`): %w", err)
	}
	hi, err := a.HostInfo()
	if err != nil {
		a.Log.Warn("host info incomplete", "err", err)
	}
	rw, err := a.Open(path)
	if err != nil {
		return false, fmt.Errorf("open %s: %w", path, err)
	}
	c := NewConn(rw, a.Log)
	defer c.Close()

	sess, err := Handshake(ctx, c, key, HelloParams{Host: hi.Hostname, OS: a.OS, AgentVer: a.Version, MACs: hi.MACs}, a.HandshakeTimeout)
	if err != nil {
		if IsDongleError(err, proto.CodeNotPaired) {
			err = fmt.Errorf("%w: dongle is not paired, run `microesp-agent pair`", err)
		}
		return false, err
	}
	a.Log.Info("session ready", "port", path, "fw", sess.Welcome.FW, "dev", sess.Welcome.Dev, "host", hi.Hostname, "macs", hi.MACs)
	if a.OnReady != nil {
		a.OnReady(sess)
	}
	return true, a.serve(ctx, c, key, sess)
}

// serve runs the ready-state loop.
func (a *Agent) serve(ctx context.Context, c *Conn, key []byte, sess *Session) error {
	// Timers are checked on a tick of period HeartbeatInterval/5 with half a
	// tick of slack, so sampling/heartbeat happen every HeartbeatInterval
	// (±half a tick) and telemetry every TelemetryInterval, instead of
	// slipping to the next tick (e.g. 7.5 s / 15 s with a coarser tick).
	period := a.HeartbeatInterval / 5
	if period <= 0 {
		period = time.Millisecond
	}
	slack := period / 2
	var (
		seq       uint32
		lastID    uint32
		throttle  = telemetry.Throttle{Interval: a.TelemetryInterval - slack, Delta: telemetry.DefaultDelta}
		lastSamp  time.Time
		tick      = time.NewTicker(period)
		sampleNow = true
	)
	defer tick.Stop()

	sample := func() error {
		lastSamp = time.Now()
		snap, err := a.Collector.Sample(ctx)
		if err != nil {
			a.Log.Warn("telemetry sample failed", "err", err)
			return nil
		}
		if !throttle.Should(snap, lastSamp) {
			return nil
		}
		seq++
		if err := c.Send(&proto.Tele{Seq: seq, CPU: snap.CPU, Mem: snap.Mem, DiskFree: snap.DiskFree, Uptime: snap.Uptime}); err != nil {
			return err
		}
		throttle.Mark(snap, lastSamp)
		return nil
	}

	for {
		if sampleNow {
			sampleNow = false
			if err := sample(); err != nil {
				return err
			}
		}
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-tick.C:
			if time.Since(lastSamp) >= a.HeartbeatInterval-slack {
				if err := sample(); err != nil {
					return err
				}
			}
			if c.SinceLastWrite() >= a.HeartbeatInterval-slack {
				if err := c.Send(&proto.HB{}); err != nil {
					return err
				}
			}
		case ib := <-c.in:
			if ib.fatal {
				return fmt.Errorf("read: %w", ib.err)
			}
			if cmd, ok := ib.msg.(*proto.Cmd); ok && (ib.err == nil || proto.CodeOf(ib.err) == proto.AckUnknownAction) {
				if err := a.handleCmd(ctx, c, key, sess, cmd, &lastID); err != nil {
					return err
				}
				continue
			}
			if ib.err != nil {
				a.Log.Warn("ignoring invalid line from dongle", "err", ib.err)
				continue
			}
			switch m := ib.msg.(type) {
			case *proto.Notice:
				a.handleNotice(ctx, m)
			case *proto.Err:
				if m.Code == proto.CodeUnauth || m.Code == proto.CodeNotPaired {
					return &DongleError{Code: m.Code} // dongle lost our session
				}
				a.Log.Warn("dongle reported error", "code", m.Code)
			default:
				a.Log.Debug("ignoring message in ready state", "t", m.Type())
			}
		}
	}
}

func (a *Agent) handleNotice(ctx context.Context, n *proto.Notice) {
	var msg string
	switch n.Action {
	case proto.ActionCancel:
		msg = "MicroESP: apagado/reinicio cancelado"
	case proto.ActionShutdown:
		msg = fmt.Sprintf("MicroESP: el equipo se APAGARÁ en %d s (cancelable desde el dongle o la app)", n.In)
	case proto.ActionReboot:
		msg = fmt.Sprintf("MicroESP: el equipo se REINICIARÁ en %d s (cancelable desde el dongle o la app)", n.In)
	}
	a.Log.Warn("notice from dongle", "action", n.Action, "in", n.In)
	go a.Executor.Notify(context.WithoutCancel(ctx), msg)
}

// handleCmd applies the command rules of cdc-v1 §3: verify sig, then
// monotonic id, then action; ack is sent BEFORE executing.
func (a *Agent) handleCmd(ctx context.Context, c *Conn, key []byte, sess *Session, cmd *proto.Cmd, lastID *uint32) error {
	reject := func(code string) error {
		a.Log.Warn("command rejected", "id", cmd.ID, "action", cmd.Action, "reason", code)
		return c.Send(&proto.Ack{ID: cmd.ID, OK: false, Err: code})
	}
	if !proto.Verify(key, proto.CmdMsg(cmd.ID, cmd.Action, sess.Na, sess.Nd), cmd.Sig) {
		return reject(proto.AckBadSig)
	}
	if cmd.ID <= *lastID {
		return reject(proto.AckReplay)
	}
	if cmd.Action != proto.ActionShutdown && cmd.Action != proto.ActionReboot {
		return reject(proto.AckUnknownAction)
	}
	*lastID = cmd.ID
	if pf, ok := a.Executor.(power.Preflighter); ok {
		if err := pf.Preflight(); err != nil {
			a.Log.Error("power backend unavailable", "backend", a.Executor.Name(), "err", err)
			return reject(proto.AckExecFailed)
		}
	}
	if err := c.Send(&proto.Ack{ID: cmd.ID, OK: true}); err != nil {
		// The dongle never learnt we accepted it: do not execute.
		return fmt.Errorf("send ack: %w", err)
	}
	a.Log.Warn("command accepted", "id", cmd.ID, "action", cmd.Action, "backend", a.Executor.Name())
	ectx, cancel := context.WithTimeout(context.WithoutCancel(ctx), a.ExecTimeout)
	defer cancel()
	if err := a.Executor.Execute(ectx, cmd.Action); err != nil {
		a.Log.Error("power action failed", "action", cmd.Action, "err", err)
	}
	return nil
}
