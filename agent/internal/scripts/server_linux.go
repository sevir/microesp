//go:build linux

package scripts

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"log/slog"
	"net"
	"os"
	"strconv"
	"sync"
	"syscall"
	"time"

	"github.com/microesp/agent/internal/proto"
)

// Server accepts start requests from the agent on a unix socket and runs
// them with a Runner, as the user the server runs as.
type Server struct {
	Runner *Runner
	Log    *slog.Logger
	// AllowUID decides which peer uids (SO_PEERCRED) may send requests;
	// other connections are closed without a reply.
	AllowUID func(uid uint32) bool
}

// AllowAgent returns the default peer check: root or the given agent uid.
func AllowAgent(agentUID uint32, haveAgent bool) func(uint32) bool {
	return func(uid uint32) bool { return uid == 0 || (haveAgent && uid == agentUID) }
}

// Serve accepts connections until ctx is cancelled. Scripts are started
// with ctx, so cancelling it also kills the scripts still running.
func (s *Server) Serve(ctx context.Context, ln net.Listener) error {
	if s.Log == nil {
		s.Log = slog.Default()
	}
	go func() { <-ctx.Done(); _ = ln.Close() }()
	var wg sync.WaitGroup
	defer wg.Wait()
	for {
		conn, err := ln.Accept()
		if err != nil {
			if ctx.Err() != nil {
				return nil
			}
			var ne net.Error
			if errors.As(err, &ne) && ne.Timeout() {
				continue
			}
			return err
		}
		wg.Add(1)
		go func() {
			defer wg.Done()
			s.handle(ctx, conn)
		}()
	}
}

func (s *Server) handle(ctx context.Context, conn net.Conn) {
	defer conn.Close()
	uid, err := peerUID(conn)
	if err != nil {
		s.Log.Warn("scripts runner: cannot read peer credentials", "err", err)
		return
	}
	if s.AllowUID == nil || !s.AllowUID(uid) {
		s.Log.Warn("scripts runner: connection refused", "peer_uid", uid)
		return
	}
	_ = conn.SetDeadline(time.Now().Add(IOTimeout))
	line, err := readLine(conn)
	if err != nil {
		s.Log.Warn("scripts runner: bad request", "err", err)
		_ = writeJSON(conn, Reply{Err: ReplyBadRequest})
		return
	}
	var req Request
	if err := json.Unmarshal(line, &req); err != nil || !proto.ValidScriptID(req.ID) {
		s.Log.Warn("scripts runner: bad request", "line", string(line))
		_ = writeJSON(conn, Reply{Err: ReplyBadRequest})
		return
	}
	job, err := s.Runner.Acquire(req.ID)
	switch {
	case errors.Is(err, ErrUnknown):
		s.Log.Warn("scripts runner: unknown script", "script", req.ID)
		_ = writeJSON(conn, Reply{Err: ReplyUnknown})
		return
	case err != nil:
		s.Log.Warn("scripts runner: script already running", "script", req.ID)
		_ = writeJSON(conn, Reply{Err: ReplyBusy})
		return
	}
	if err := writeJSON(conn, Reply{OK: true}); err != nil {
		job.Release() // the agent never learnt it was accepted
		s.Log.Warn("scripts runner: reply failed, script not started", "script", req.ID, "err", err)
		return
	}
	job.Start(ctx)
}

func peerUID(conn net.Conn) (uint32, error) {
	uc, ok := conn.(*net.UnixConn)
	if !ok {
		return 0, errors.New("not a unix socket")
	}
	raw, err := uc.SyscallConn()
	if err != nil {
		return 0, err
	}
	var cred *syscall.Ucred
	var cerr error
	if err := raw.Control(func(fd uintptr) {
		cred, cerr = syscall.GetsockoptUcred(int(fd), syscall.SOL_SOCKET, syscall.SO_PEERCRED)
	}); err != nil {
		return 0, err
	}
	if cerr != nil {
		return 0, cerr
	}
	return cred.Uid, nil
}

// listenFDsStart is the first fd passed by systemd socket activation.
const listenFDsStart = 3

// activationFD returns the listening fd passed by systemd (sd_listen_fds):
// ok is false when the process was not socket-activated.
func activationFD(getenv func(string) string, pid int) (fd int, ok bool, err error) {
	ps, ns := getenv("LISTEN_PID"), getenv("LISTEN_FDS")
	if ps == "" && ns == "" {
		return 0, false, nil
	}
	p, err := strconv.Atoi(ps)
	if err != nil || p != pid {
		return 0, false, fmt.Errorf("LISTEN_PID=%q is not this process (%d)", ps, pid)
	}
	n, err := strconv.Atoi(ns)
	if err != nil || n < 1 {
		return 0, false, fmt.Errorf("LISTEN_FDS=%q: no socket passed", ns)
	}
	if n > 1 {
		return 0, false, fmt.Errorf("LISTEN_FDS=%d: expected exactly one socket", n)
	}
	return listenFDsStart, true, nil
}

// Listen returns the socket-activated listener when LISTEN_FDS/LISTEN_PID
// are set for this process, otherwise a new unix socket at path (mode 0660,
// replacing a stale socket file). activated tells which one it is.
func Listen(path string) (ln net.Listener, activated bool, err error) {
	fd, ok, err := activationFD(os.Getenv, os.Getpid())
	if err != nil {
		return nil, false, err
	}
	if ok {
		for _, k := range []string{"LISTEN_PID", "LISTEN_FDS", "LISTEN_FDNAMES"} {
			_ = os.Unsetenv(k) // not inherited by the scripts
		}
		ln, err := listenerFromFD(fd)
		if err != nil {
			return nil, false, fmt.Errorf("socket from systemd: %w", err)
		}
		return ln, true, nil
	}
	if path == "" {
		return nil, false, errors.New("not socket-activated and no --listen path given")
	}
	if fi, err := os.Lstat(path); err == nil && fi.Mode()&os.ModeSocket != 0 {
		_ = os.Remove(path)
	}
	ln, err = net.Listen("unix", path)
	if err != nil {
		return nil, false, err
	}
	if err := os.Chmod(path, 0o660); err != nil {
		ln.Close()
		return nil, false, err
	}
	return ln, false, nil
}

// listenerFromFD wraps an inherited listening fd. The fd itself is closed:
// net.FileListener works on a close-on-exec duplicate, so the scripts do
// not inherit the socket.
func listenerFromFD(fd int) (net.Listener, error) {
	f := os.NewFile(uintptr(fd), "systemd-socket")
	defer f.Close()
	return net.FileListener(f)
}
