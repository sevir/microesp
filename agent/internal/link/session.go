package link

import (
	"context"
	"errors"
	"fmt"
	"time"

	"github.com/microesp/agent/internal/proto"
	"github.com/microesp/agent/internal/telemetry"
)

// Handshake / pairing errors.
var (
	ErrBadWelcomeSig = errors.New("link: welcome signature invalid (wrong key or impostor device)")
	ErrBadPairOKSig  = errors.New("link: pair_ok signature invalid")
)

// HandshakeTimeout is the default wait for welcome/ready (cdc-v1 §3).
const HandshakeTimeout = 3 * time.Second

// Session holds the nonces of an authenticated session.
type Session struct {
	Na, Nd  string
	Welcome *proto.Welcome
}

// HelloParams are the fields of hello.
type HelloParams struct {
	Host     string
	OS       string
	AgentVer string
	MACs     []string
}

// Handshake runs hello → welcome(verify) → auth → ready on c.
func Handshake(ctx context.Context, c *Conn, key []byte, hp HelloParams, timeout time.Duration) (*Session, error) {
	na, err := proto.NewNonce()
	if err != nil {
		return nil, err
	}
	macs := hp.MACs
	if macs == nil {
		macs = []string{}
	}
	if len(macs) > proto.MaxMACs {
		macs = macs[:proto.MaxMACs]
	}
	hello := &proto.Hello{V: proto.Version, Host: telemetry.TruncateHost(hp.Host), OS: hp.OS, AgentVer: hp.AgentVer, MACs: macs, Nonce: na}
	if err := c.Send(hello); err != nil {
		return nil, fmt.Errorf("send hello: %w", err)
	}
	m, err := c.Expect(ctx, timeout, func(m proto.Message) bool { return m.Type() == proto.TypeWelcome })
	if err != nil {
		return nil, fmt.Errorf("waiting welcome: %w", err)
	}
	w := m.(*proto.Welcome)
	if !proto.Verify(key, proto.WelcomeMsg(na, w.Nonce), w.Sig) {
		return nil, ErrBadWelcomeSig
	}
	if err := c.Send(&proto.Auth{Sig: proto.Sign(key, proto.AuthMsg(na, w.Nonce))}); err != nil {
		return nil, fmt.Errorf("send auth: %w", err)
	}
	if _, err := c.Expect(ctx, timeout, func(m proto.Message) bool { return m.Type() == proto.TypeReady }); err != nil {
		return nil, fmt.Errorf("waiting ready: %w", err)
	}
	return &Session{Na: na, Nd: w.Nonce, Welcome: w}, nil
}

// Pair runs the pairing exchange (cdc-v1 §4) and returns the verified key.
// The caller must persist the key only when err is nil.
func Pair(ctx context.Context, c *Conn, code string, timeout time.Duration) ([]byte, error) {
	if !proto.ValidCode(code) {
		return nil, errors.New("pairing code must be exactly 6 digits")
	}
	na, err := proto.NewNonce()
	if err != nil {
		return nil, err
	}
	if err := c.Send(&proto.Pair{V: proto.Version, Nonce: na}); err != nil {
		return nil, fmt.Errorf("send pair: %w", err)
	}
	m, err := c.Expect(ctx, timeout, func(m proto.Message) bool { return m.Type() == proto.TypePairChal })
	if err != nil {
		return nil, fmt.Errorf("waiting pair_chal: %w", err)
	}
	nd := m.(*proto.PairChal).Nonce
	key, err := proto.DerivePairKey(code, na, nd)
	if err != nil {
		return nil, err
	}
	if err := c.Send(&proto.PairConfirm{Sig: proto.Sign(key, proto.PairMsg(na, nd))}); err != nil {
		return nil, fmt.Errorf("send pair_confirm: %w", err)
	}
	m, err = c.Expect(ctx, timeout, func(m proto.Message) bool { return m.Type() == proto.TypePairOK })
	if err != nil {
		return nil, fmt.Errorf("waiting pair_ok: %w", err)
	}
	if !proto.Verify(key, proto.PairOKMsg(na, nd), m.(*proto.PairOK).Sig) {
		return nil, ErrBadPairOKSig
	}
	return key, nil
}
