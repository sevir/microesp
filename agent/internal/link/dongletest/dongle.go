// Package dongletest provides an in-memory fake MicroESP dongle that
// speaks cdc-v1 over a net.Pipe, for tests only.
package dongletest

import (
	"io"
	"net"
	"sync"

	"github.com/microesp/agent/internal/proto"
)

// Behaviour knobs.
type Mode int

const (
	Normal        Mode = iota
	NotPaired          // answer hello with err not_paired
	BadWelcomeSig      // sign welcome with a wrong key
	Silent             // never answer hello
	RejectAuth         // answer auth with err unauth even if valid
)

// Dongle is the fake device side of a pipe.
type Dongle struct {
	Key      []byte
	Mode     Mode
	PairCode string // expected pairing code ("" = refuse pairing)
	FW, Dev  string

	conn net.Conn
	wmu  sync.Mutex

	mu      sync.Mutex
	na, nd  string
	ready   bool
	got     []proto.Message
	acks    map[uint32]proto.Ack
	newKey  []byte
	changed chan struct{}
	closed  chan struct{}
}

// New returns the agent side of a pipe and a running fake dongle.
func New(key []byte, mode Mode) (io.ReadWriteCloser, *Dongle) {
	a, b := net.Pipe()
	d := &Dongle{Key: key, Mode: mode, FW: "1.0.0", Dev: "907069f662dc", conn: b,
		acks: map[uint32]proto.Ack{}, changed: make(chan struct{}), closed: make(chan struct{})}
	go d.serve()
	return a, d
}

// Close unplugs the dongle.
func (d *Dongle) Close() { _ = d.conn.Close() }

// Done is closed when the dongle stops reading (agent closed / unplugged).
func (d *Dongle) Done() <-chan struct{} { return d.closed }

func (d *Dongle) notify() {
	close(d.changed)
	d.changed = make(chan struct{})
}

// Changed returns a channel closed on the next received message.
func (d *Dongle) Changed() <-chan struct{} {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.changed
}

// Send writes a message to the agent.
func (d *Dongle) Send(m proto.Message) error {
	b, err := proto.Encode(m)
	if err != nil {
		return err
	}
	return d.SendRaw(b)
}

// SendRaw writes raw bytes to the agent.
func (d *Dongle) SendRaw(b []byte) error {
	d.wmu.Lock()
	defer d.wmu.Unlock()
	_, err := d.conn.Write(b)
	return err
}

// Nonces returns the current session nonces.
func (d *Dongle) Nonces() (na, nd string) {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.na, d.nd
}

// Ready reports whether the session is authenticated.
func (d *Dongle) Ready() bool {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.ready
}

// SignedCmd builds a cmd signed for the current session.
func (d *Dongle) SignedCmd(id uint32, action string) *proto.Cmd {
	d.mu.Lock()
	defer d.mu.Unlock()
	return &proto.Cmd{ID: id, Action: action, Sig: proto.Sign(d.Key, proto.CmdMsg(id, action, d.na, d.nd))}
}

// Received returns a copy of the messages received from the agent.
func (d *Dongle) Received() []proto.Message {
	d.mu.Lock()
	defer d.mu.Unlock()
	return append([]proto.Message(nil), d.got...)
}

// Ack returns the ack received for id, if any.
func (d *Dongle) Ack(id uint32) (proto.Ack, bool) {
	d.mu.Lock()
	defer d.mu.Unlock()
	a, ok := d.acks[id]
	return a, ok
}

// PairedKey returns the key stored after a successful pairing.
func (d *Dongle) PairedKey() []byte {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.newKey
}

func (d *Dongle) serve() {
	defer close(d.closed)
	lr := proto.NewLineReader(d.conn)
	for {
		line, err := lr.ReadLine()
		if err == proto.ErrTooLong {
			_ = d.Send(&proto.Err{Code: proto.CodeTooLong})
			continue
		}
		if err != nil {
			return
		}
		m, err := proto.Decode(line)
		if err != nil {
			code := proto.CodeOf(err)
			if code == "" {
				code = proto.CodeBadMsg
			}
			_ = d.Send(&proto.Err{Code: code})
			continue
		}
		d.handle(m)
		d.mu.Lock()
		d.got = append(d.got, m)
		d.notify()
		d.mu.Unlock()
	}
}

func (d *Dongle) handle(m proto.Message) {
	switch x := m.(type) {
	case *proto.Hello:
		d.mu.Lock()
		d.ready = false
		d.na = x.Nonce
		d.nd, _ = proto.NewNonce()
		na, nd := d.na, d.nd
		d.mu.Unlock()
		switch d.Mode {
		case Silent:
			return
		case NotPaired:
			_ = d.Send(&proto.Err{Code: proto.CodeNotPaired})
			return
		}
		key := d.Key
		if d.Mode == BadWelcomeSig {
			key = []byte("impostor")
		}
		_ = d.Send(&proto.Welcome{V: 1, FW: d.FW, Dev: d.Dev, Nonce: nd, Sig: proto.Sign(key, proto.WelcomeMsg(na, nd))})
	case *proto.Auth:
		na, nd := d.Nonces()
		if d.Mode == RejectAuth || na == "" || !proto.Verify(d.Key, proto.AuthMsg(na, nd), x.Sig) {
			_ = d.Send(&proto.Err{Code: proto.CodeUnauth})
			return
		}
		d.mu.Lock()
		d.ready = true
		d.mu.Unlock()
		_ = d.Send(&proto.Ready{})
	case *proto.Tele, *proto.HB:
		if !d.Ready() {
			_ = d.Send(&proto.Err{Code: proto.CodeUnauth})
		}
	case *proto.Ack:
		if !d.Ready() {
			_ = d.Send(&proto.Err{Code: proto.CodeUnauth})
			return
		}
		d.mu.Lock()
		d.acks[x.ID] = *x
		d.mu.Unlock()
	case *proto.Pair:
		nd, _ := proto.NewNonce()
		d.mu.Lock()
		d.na, d.nd = x.Nonce, nd
		d.mu.Unlock()
		_ = d.Send(&proto.PairChal{Nonce: nd})
	case *proto.PairConfirm:
		na, nd := d.Nonces()
		k, err := proto.DerivePairKey(d.PairCode, na, nd)
		if err != nil || !proto.Verify(k, proto.PairMsg(na, nd), x.Sig) {
			_ = d.Send(&proto.Err{Code: proto.CodePairFailed})
			return
		}
		d.mu.Lock()
		d.newKey, d.Key = k, k
		d.mu.Unlock()
		_ = d.Send(&proto.PairOK{Sig: proto.Sign(k, proto.PairOKMsg(na, nd))})
	}
}
