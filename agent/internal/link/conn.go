package link

import (
	"context"
	"errors"
	"io"
	"log/slog"
	"sync"
	"time"

	"github.com/microesp/agent/internal/proto"
)

// ErrTimeout is returned when the dongle does not answer in time.
var ErrTimeout = errors.New("link: timeout waiting for dongle")

// WriteTimeout bounds a single line write; a stalled port is closed.
var WriteTimeout = 5 * time.Second

type inbound struct {
	msg   proto.Message
	err   error // decode error (non-fatal) or read error (fatal, fatal=true)
	fatal bool
}

// Conn is a framed protocol connection over a byte stream.
type Conn struct {
	rw        io.ReadWriteCloser
	log       *slog.Logger
	in        chan inbound
	done      chan struct{}
	wg        sync.WaitGroup
	closeOnce sync.Once
	lastWrite time.Time
}

// NewConn starts the reader goroutine over rw.
func NewConn(rw io.ReadWriteCloser, log *slog.Logger) *Conn {
	if log == nil {
		log = slog.Default()
	}
	c := &Conn{rw: rw, log: log, in: make(chan inbound, 16), done: make(chan struct{})}
	c.wg.Add(1)
	go c.readLoop()
	return c
}

func (c *Conn) readLoop() {
	defer c.wg.Done()
	lr := proto.NewLineReader(c.rw)
	for {
		line, err := lr.ReadLine()
		var ib inbound
		switch {
		case errors.Is(err, proto.ErrTooLong):
			ib = inbound{err: err}
		case err != nil:
			ib = inbound{err: err, fatal: true}
		default:
			c.log.Debug("rx", "line", string(line))
			m, derr := proto.Decode(line)
			ib = inbound{msg: m, err: derr}
		}
		select {
		case c.in <- ib:
		case <-c.done:
			return
		}
		if ib.fatal {
			return
		}
	}
}

// Close closes the port and waits for the reader to exit.
func (c *Conn) Close() error {
	var err error
	c.closeOnce.Do(func() {
		close(c.done)
		err = c.rw.Close()
		c.wg.Wait()
	})
	return err
}

// Send encodes and writes m as one line.
func (c *Conn) Send(m proto.Message) error {
	b, err := proto.Encode(m)
	if err != nil {
		return err
	}
	c.log.Debug("tx", "line", string(b[:len(b)-1]))
	res := make(chan error, 1)
	go func() {
		_, err := c.rw.Write(b)
		res <- err
	}()
	t := time.NewTimer(WriteTimeout)
	defer t.Stop()
	select {
	case err := <-res:
		if err == nil {
			c.lastWrite = time.Now()
		}
		return err
	case <-t.C:
		_ = c.rw.Close() // unblock the writer; the session is over
		return errors.New("link: write timeout")
	}
}

// SinceLastWrite returns the time elapsed since the last successful write.
func (c *Conn) SinceLastWrite() time.Duration { return time.Since(c.lastWrite) }

// Recv returns the next inbound item. Decode errors are returned with a
// nil fatal flag so callers can skip them; read errors are fatal.
func (c *Conn) recv(ctx context.Context, timer <-chan time.Time) (inbound, error) {
	select {
	case ib := <-c.in:
		return ib, nil
	case <-timer:
		return inbound{}, ErrTimeout
	case <-ctx.Done():
		return inbound{}, ctx.Err()
	}
}

// Expect waits up to timeout for a message accepted by want. Decode
// errors and unrelated messages are logged and skipped; an err message is
// returned as *DongleError.
func (c *Conn) Expect(ctx context.Context, timeout time.Duration, want func(proto.Message) bool) (proto.Message, error) {
	t := time.NewTimer(timeout)
	defer t.Stop()
	for {
		ib, err := c.recv(ctx, t.C)
		if err != nil {
			return nil, err
		}
		if ib.fatal {
			return nil, ib.err
		}
		if ib.err != nil {
			c.log.Warn("ignoring invalid line from dongle", "err", ib.err)
			continue
		}
		if e, ok := ib.msg.(*proto.Err); ok {
			return nil, &DongleError{Code: e.Code}
		}
		if want(ib.msg) {
			return ib.msg, nil
		}
		c.log.Debug("ignoring unexpected message", "t", ib.msg.Type())
	}
}

// DongleError is an err{code} received from the dongle.
type DongleError struct{ Code string }

func (e *DongleError) Error() string { return "dongle error: " + e.Code }

// IsDongleError reports whether err is a DongleError with code.
func IsDongleError(err error, code string) bool {
	var de *DongleError
	return errors.As(err, &de) && de.Code == code
}
