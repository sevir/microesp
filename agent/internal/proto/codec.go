package proto

import (
	"bufio"
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"regexp"
)

// Error is a protocol error carrying one of the spec error codes.
type Error struct {
	Code string
	Msg  string
}

func (e *Error) Error() string {
	if e.Msg == "" {
		return "proto: " + e.Code
	}
	return "proto: " + e.Code + ": " + e.Msg
}

// ErrTooLong is returned by the line reader for a line over MaxLine bytes.
var ErrTooLong = &Error{Code: CodeTooLong, Msg: "line exceeds 512 bytes"}

func errf(code, format string, a ...any) *Error {
	return &Error{Code: code, Msg: fmt.Sprintf(format, a...)}
}

// CodeOf returns the protocol code of err, or "" if it is not a *Error.
func CodeOf(err error) string {
	var pe *Error
	if errors.As(err, &pe) {
		return pe.Code
	}
	return ""
}

var (
	reNonce = regexp.MustCompile(`^[0-9a-f]{16}$`)
	reSig   = regexp.MustCompile(`^[0-9a-f]{64}$`)
	reDev   = regexp.MustCompile(`^[0-9a-f]{12}$`)
	reMAC   = regexp.MustCompile(`^[0-9a-f]{2}(:[0-9a-f]{2}){5}$`)
)

// ValidNonce reports whether s is 16 lowercase hex chars.
func ValidNonce(s string) bool { return reNonce.MatchString(s) }

// ValidSig reports whether s is 64 lowercase hex chars.
func ValidSig(s string) bool { return reSig.MatchString(s) }

// ValidMAC reports whether s is a lowercase aa:bb:cc:dd:ee:ff MAC.
func ValidMAC(s string) bool { return reMAC.MatchString(s) }

// Encode serialises m as one JSON line terminated by '\n'.
// It fails if the line would exceed MaxLine bytes.
func Encode(m Message) ([]byte, error) {
	m.head().T = m.Type()
	var buf bytes.Buffer
	enc := json.NewEncoder(&buf)
	enc.SetEscapeHTML(false)
	if err := enc.Encode(m); err != nil { // appends '\n'
		return nil, err
	}
	if buf.Len() > MaxLine {
		return nil, errf(CodeTooLong, "encoded %s is %d bytes", m.Type(), buf.Len())
	}
	return buf.Bytes(), nil
}

// Decode parses and validates one line (without terminator).
//
// For a cmd whose only defect is an unknown action, Decode returns the
// parsed *Cmd together with an Error of code AckUnknownAction, so the
// caller can still answer with an ack.
func Decode(line []byte) (Message, error) {
	if len(line)+1 > MaxLine {
		return nil, ErrTooLong
	}
	var raw map[string]json.RawMessage
	if err := json.Unmarshal(line, &raw); err != nil {
		return nil, errf(CodeBadMsg, "not a JSON object: %v", err)
	}
	var t string
	if err := json.Unmarshal(raw["t"], &t); err != nil || t == "" {
		return nil, errf(CodeBadMsg, "missing or invalid field t")
	}
	m := newByType(t)
	if m == nil {
		return nil, errf(CodeBadMsg, "unknown type %q", t)
	}
	// Version is checked first so that hello{v:2} yields unsupported_version
	// even if other fields of a future version differ.
	if rv, ok := raw["v"]; ok && (t == TypeHello || t == TypeWelcome || t == TypePair) {
		var v int
		if err := json.Unmarshal(rv, &v); err != nil {
			return nil, errf(CodeBadMsg, "invalid v")
		}
		if v != Version {
			return nil, errf(CodeUnsupportedVersion, "v=%d", v)
		}
	}
	for _, k := range requiredFields[t] {
		if _, ok := raw[k]; !ok {
			return nil, errf(CodeBadMsg, "%s: missing field %q", t, k)
		}
	}
	if err := json.Unmarshal(line, m); err != nil {
		return nil, errf(CodeBadMsg, "%s: %v", t, err)
	}
	if err := Validate(m); err != nil {
		if c, ok := m.(*Cmd); ok && CodeOf(err) == AckUnknownAction {
			return c, err
		}
		return nil, err
	}
	return m, nil
}

func tenths(name string, v int) error {
	if v < 0 || v > MaxTenths {
		return errf(CodeBadMsg, "%s out of range 0..1000: %d", name, v)
	}
	return nil
}

// Validate checks field ranges and formats of a message.
func Validate(m Message) error {
	switch x := m.(type) {
	case *Hello:
		if x.V != Version {
			return errf(CodeUnsupportedVersion, "v=%d", x.V)
		}
		if len(x.Host) > MaxHostLen {
			return errf(CodeBadMsg, "host longer than %d bytes", MaxHostLen)
		}
		if x.OS != "linux" && x.OS != "windows" {
			return errf(CodeBadMsg, "invalid os %q", x.OS)
		}
		if x.AgentVer == "" {
			return errf(CodeBadMsg, "empty agent_ver")
		}
		if len(x.MACs) > MaxMACs {
			return errf(CodeBadMsg, "more than %d macs", MaxMACs)
		}
		for _, mac := range x.MACs {
			if !ValidMAC(mac) {
				return errf(CodeBadMsg, "invalid mac %q", mac)
			}
		}
		if !ValidNonce(x.Nonce) {
			return errf(CodeBadMsg, "invalid nonce")
		}
	case *Welcome:
		if x.V != Version {
			return errf(CodeUnsupportedVersion, "v=%d", x.V)
		}
		if x.FW == "" {
			return errf(CodeBadMsg, "empty fw")
		}
		if !reDev.MatchString(x.Dev) {
			return errf(CodeBadMsg, "invalid dev")
		}
		if !ValidNonce(x.Nonce) {
			return errf(CodeBadMsg, "invalid nonce")
		}
		if !ValidSig(x.Sig) {
			return errf(CodeBadMsg, "invalid sig")
		}
	case *Auth:
		if !ValidSig(x.Sig) {
			return errf(CodeBadMsg, "invalid sig")
		}
	case *Tele:
		for _, f := range []struct {
			n string
			v int
		}{{"cpu", x.CPU}, {"mem", x.Mem}, {"disk_free", x.DiskFree}} {
			if err := tenths(f.n, f.v); err != nil {
				return err
			}
		}
	case *Ack:
		switch {
		case x.OK && x.Err != "":
			return errf(CodeBadMsg, "ack ok with err")
		case !x.OK:
			switch x.Err {
			case AckBadSig, AckReplay, AckExecFailed, AckUnknownAction:
			default:
				return errf(CodeBadMsg, "invalid ack err %q", x.Err)
			}
		}
	case *Notice:
		switch x.Action {
		case ActionShutdown, ActionReboot, ActionCancel:
		default:
			return errf(CodeBadMsg, "invalid notice action %q", x.Action)
		}
	case *Cmd:
		if x.Action != ActionShutdown && x.Action != ActionReboot {
			return errf(AckUnknownAction, "action %q", x.Action)
		}
	case *Err:
		if x.Code == "" {
			return errf(CodeBadMsg, "empty code")
		}
	case *Pair:
		if x.V != Version {
			return errf(CodeUnsupportedVersion, "v=%d", x.V)
		}
		if !ValidNonce(x.Nonce) {
			return errf(CodeBadMsg, "invalid nonce")
		}
	case *PairChal:
		if !ValidNonce(x.Nonce) {
			return errf(CodeBadMsg, "invalid nonce")
		}
	case *PairConfirm:
		if !ValidSig(x.Sig) {
			return errf(CodeBadMsg, "invalid sig")
		}
	case *PairOK:
		if !ValidSig(x.Sig) {
			return errf(CodeBadMsg, "invalid sig")
		}
	}
	return nil
}

// LineReader splits a byte stream into protocol lines, enforcing MaxLine.
type LineReader struct {
	r *bufio.Reader
}

// NewLineReader wraps r.
func NewLineReader(r io.Reader) *LineReader {
	return &LineReader{r: bufio.NewReaderSize(r, 1024)}
}

// ReadLine returns the next non-empty line without its "\n" / "\r\n".
// A line longer than MaxLine (terminator included) is discarded up to the
// next '\n' and ErrTooLong is returned; the reader stays usable.
func (lr *LineReader) ReadLine() ([]byte, error) {
	for {
		var line []byte
		overflow := false
		for {
			chunk, err := lr.r.ReadSlice('\n')
			if !overflow {
				line = append(line, chunk...)
				if len(line) > MaxLine {
					overflow = true
					line = nil
				}
			}
			if err == bufio.ErrBufferFull {
				continue
			}
			if err != nil {
				return nil, err // partial line at EOF is dropped
			}
			break
		}
		if overflow {
			return nil, ErrTooLong
		}
		line = bytes.TrimSuffix(line[:len(line)-1], []byte("\r"))
		if len(line) == 0 {
			continue
		}
		return line, nil
	}
}
