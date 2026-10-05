package scripts

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"time"
)

// Wire protocol between the agent and the scripts runner (unix socket):
// one JSON line per connection each way.
//
//	agent  -> runner  {"id":"backup"}
//	runner -> agent   {"ok":true} | {"ok":false,"err":"unknown"|"busy"|"bad_request"}
//
// "ok" means the runner accepted the script and started it in the
// background; the outcome is only logged by the runner.

// MaxRequestLine bounds a request or reply line, terminator included.
const MaxRequestLine = 256

// IOTimeout bounds each read and write on a runner connection.
const IOTimeout = 2 * time.Second

// Reply error codes.
const (
	ReplyUnknown    = "unknown"
	ReplyBusy       = "busy"
	ReplyBadRequest = "bad_request"
)

// ErrBadRequest is returned when the runner rejects a malformed request.
var ErrBadRequest = errors.New("scripts: runner rejected the request as malformed")

// Request asks the runner to start a script.
type Request struct {
	ID string `json:"id"`
}

// Reply answers a Request.
type Reply struct {
	OK  bool   `json:"ok"`
	Err string `json:"err,omitempty"`
}

// errLineTooLong is returned by readLine for an oversized line.
var errLineTooLong = fmt.Errorf("line longer than %d bytes", MaxRequestLine)

// readLine reads one '\n'-terminated line of at most MaxRequestLine bytes
// (terminator included).
func readLine(r io.Reader) ([]byte, error) {
	br := bufio.NewReaderSize(io.LimitReader(r, MaxRequestLine), MaxRequestLine)
	line, err := br.ReadSlice('\n')
	switch {
	case errors.Is(err, bufio.ErrBufferFull), errors.Is(err, io.EOF) && len(line) >= MaxRequestLine:
		return nil, errLineTooLong
	case err != nil:
		return nil, err
	}
	return line, nil
}

func writeJSON(w io.Writer, v any) error {
	b, err := json.Marshal(v)
	if err != nil {
		return err
	}
	_, err = w.Write(append(b, '\n'))
	return err
}

// Client talks to a scripts runner listening on a unix socket.
type Client struct {
	Path    string
	Timeout time.Duration // whole exchange; default IOTimeout
}

// Start asks the runner to start script id. It returns nil when the runner
// started it, ErrUnknown, ErrBusy, ErrBadRequest, or another error when the
// runner is unreachable or does not answer in time.
func (c *Client) Start(ctx context.Context, id string) error {
	timeout := c.Timeout
	if timeout <= 0 {
		timeout = IOTimeout
	}
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	var d net.Dialer
	conn, err := d.DialContext(ctx, "unix", c.Path)
	if err != nil {
		return fmt.Errorf("scripts runner unreachable: %w", err)
	}
	defer conn.Close()
	deadline, _ := ctx.Deadline()
	_ = conn.SetDeadline(deadline)
	if err := writeJSON(conn, Request{ID: id}); err != nil {
		return fmt.Errorf("scripts runner: send: %w", err)
	}
	line, err := readLine(conn)
	if err != nil {
		return fmt.Errorf("scripts runner: no reply: %w", err)
	}
	var rep Reply
	if err := json.Unmarshal(line, &rep); err != nil {
		return fmt.Errorf("scripts runner: bad reply %q: %w", line, err)
	}
	switch {
	case rep.OK:
		return nil
	case rep.Err == ReplyUnknown:
		return ErrUnknown
	case rep.Err == ReplyBusy:
		return ErrBusy
	case rep.Err == ReplyBadRequest:
		return ErrBadRequest
	}
	return fmt.Errorf("scripts runner: error %q", rep.Err)
}
