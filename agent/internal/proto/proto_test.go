package proto

import (
	"bytes"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"os"
	"reflect"
	"strings"
	"testing"
)

// vectorsPath is the normative cross-implementation contract.
const vectorsPath = "../../../protocol/testdata/vectors.json"

type vectors struct {
	Pairing struct {
		Code           string `json:"code"`
		AgentNonce     string `json:"agent_nonce"`
		DongleNonce    string `json:"dongle_nonce"`
		KeyHex         string `json:"key_hex"`
		PairConfirmSig string `json:"pair_confirm_sig"`
		PairOKSig      string `json:"pair_ok_sig"`
	} `json:"pairing"`
	Session struct {
		KeyHex      string `json:"key_hex"`
		AgentNonce  string `json:"agent_nonce"`
		DongleNonce string `json:"dongle_nonce"`
		WelcomeSig  string `json:"welcome_sig"`
		AuthSig     string `json:"auth_sig"`
		Cmds        []struct {
			ID     uint32 `json:"id"`
			Action string `json:"action"`
			Sig    string `json:"sig"`
		} `json:"cmds"`
	} `json:"session"`
	Messages struct {
		Valid   []json.RawMessage `json:"valid"`
		Invalid []struct {
			Line string `json:"line"`
			Why  string `json:"why"`
		} `json:"invalid"`
	} `json:"messages"`
	CmdBadSig struct {
		ID     uint32 `json:"id"`
		Action string `json:"action"`
		Sig    string `json:"sig"`
		Expect string `json:"expect"`
	} `json:"cmd_bad_sig"`
}

func loadVectors(t *testing.T) vectors {
	t.Helper()
	b, err := os.ReadFile(vectorsPath)
	if err != nil {
		t.Fatalf("read vectors: %v", err)
	}
	var v vectors
	if err := json.Unmarshal(b, &v); err != nil {
		t.Fatalf("parse vectors: %v", err)
	}
	return v
}

func mustHex(t *testing.T, s string) []byte {
	t.Helper()
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func TestPairingVectors(t *testing.T) {
	p := loadVectors(t).Pairing
	k, err := DerivePairKey(p.Code, p.AgentNonce, p.DongleNonce)
	if err != nil {
		t.Fatal(err)
	}
	if got := hex.EncodeToString(k); got != p.KeyHex {
		t.Fatalf("key = %s, want %s", got, p.KeyHex)
	}
	if got := Sign(k, PairMsg(p.AgentNonce, p.DongleNonce)); got != p.PairConfirmSig {
		t.Errorf("pair_confirm sig = %s", got)
	}
	if got := Sign(k, PairOKMsg(p.AgentNonce, p.DongleNonce)); got != p.PairOKSig {
		t.Errorf("pair_ok sig = %s", got)
	}
	if !Verify(k, PairOKMsg(p.AgentNonce, p.DongleNonce), p.PairOKSig) {
		t.Error("verify pair_ok failed")
	}
}

func TestSessionVectors(t *testing.T) {
	v := loadVectors(t)
	s := v.Session
	k := mustHex(t, s.KeyHex)
	if got := Sign(k, WelcomeMsg(s.AgentNonce, s.DongleNonce)); got != s.WelcomeSig {
		t.Errorf("welcome sig = %s", got)
	}
	if got := Sign(k, AuthMsg(s.AgentNonce, s.DongleNonce)); got != s.AuthSig {
		t.Errorf("auth sig = %s", got)
	}
	for _, c := range s.Cmds {
		if !Verify(k, CmdMsg(c.ID, c.Action, s.AgentNonce, s.DongleNonce), c.Sig) {
			t.Errorf("cmd %d %s sig mismatch", c.ID, c.Action)
		}
	}
	b := v.CmdBadSig
	if b.Expect != "reject" {
		t.Fatalf("unexpected expect %q", b.Expect)
	}
	if Verify(k, CmdMsg(b.ID, b.Action, s.AgentNonce, s.DongleNonce), b.Sig) {
		t.Error("bad sig accepted")
	}
	// Signature bound to nonces: swapping them must fail.
	if Verify(k, WelcomeMsg(s.DongleNonce, s.AgentNonce), s.WelcomeSig) {
		t.Error("welcome sig verified with swapped nonces")
	}
}

func TestValidMessagesRoundTrip(t *testing.T) {
	for _, raw := range loadVectors(t).Messages.Valid {
		m, err := Decode(raw)
		if err != nil {
			t.Errorf("Decode(%s): %v", raw, err)
			continue
		}
		out, err := Encode(m)
		if err != nil {
			t.Errorf("Encode(%s): %v", raw, err)
			continue
		}
		if out[len(out)-1] != '\n' {
			t.Errorf("Encode output not newline terminated")
		}
		var want, got any
		_ = json.Unmarshal(raw, &want)
		_ = json.Unmarshal(out, &got)
		if !reflect.DeepEqual(want, got) {
			t.Errorf("round trip mismatch:\n in  %s\n out %s", raw, out)
		}
	}
}

func TestInvalidMessages(t *testing.T) {
	for _, iv := range loadVectors(t).Messages.Invalid {
		t.Run(iv.Why, func(t *testing.T) {
			m, err := Decode([]byte(iv.Line))
			if err == nil {
				t.Fatalf("Decode accepted invalid line %q", iv.Line)
			}
			if CodeOf(err) == "" {
				t.Errorf("error %v carries no protocol code", err)
			}
			if CodeOf(err) != AckUnknownAction && m != nil {
				t.Errorf("message returned with error %v", err)
			}
			// The line reader must also reject over-long lines.
			if len(iv.Line)+1 > MaxLine {
				lr := NewLineReader(strings.NewReader(iv.Line + "\n{\"t\":\"hb\"}\n"))
				if _, err := lr.ReadLine(); !errors.Is(err, ErrTooLong) {
					t.Errorf("ReadLine err = %v, want ErrTooLong", err)
				}
				l, err := lr.ReadLine()
				if err != nil || string(l) != `{"t":"hb"}` {
					t.Errorf("reader not resynchronised: %q %v", l, err)
				}
			}
		})
	}
}

func TestInvalidCodes(t *testing.T) {
	cases := map[string]string{
		`{"t":"hello","v":2,"nonce":"a1b2c3d4e5f60718"}`: CodeUnsupportedVersion,
		`{"t":"nope"}`: CodeBadMsg,
		`[1,2,3]`:      CodeBadMsg,
		`{"t":"cmd","id":1,"action":"format_disk","sig":"00"}`:                                                         AckUnknownAction,
		`{"t":"tele","seq":1,"cpu":1001,"mem":0,"disk_free":0,"uptime":0}`:                                             CodeBadMsg,
		`{"t":"tele","seq":1,"cpu":-1,"mem":0,"disk_free":0,"uptime":0}`:                                               CodeBadMsg,
		`{"t":"tele","seq":1,"cpu":1,"mem":0,"disk_free":0}`:                                                           CodeBadMsg,
		`{"t":"tele","seq":-1,"cpu":1,"mem":0,"disk_free":0,"uptime":0}`:                                               CodeBadMsg,
		`{"t":"welcome","v":1,"fw":"1","dev":"XX","nonce":"0011223344556677","sig":"` + strings.Repeat("0", 64) + `"}`: CodeBadMsg,
		`{"t":"welcome","v":3,"fw":"1","dev":"907069f662dc","nonce":"0011223344556677","sig":"x"}`:                     CodeUnsupportedVersion,
		`{"t":"pair_chal","nonce":"ABCDEF0123456789"}`:                                                                 CodeBadMsg,
		`{"t":"notice","action":"explode","in":3}`:                                                                     CodeBadMsg,
		`{"t":"ack","id":1,"ok":true,"err":"replay"}`:                                                                  CodeBadMsg,
		`{"t":"ack","id":1,"ok":false,"err":"whatever"}`:                                                               CodeBadMsg,
		`{"t":"err","code":""}`:                            CodeBadMsg,
		`{"t":1}`:                                          CodeBadMsg,
		`{"t":"pair","v":1,"nonce":"zz"}`:                  CodeBadMsg,
		`{"t":"pair","v":"1","nonce":"zz"}`:                CodeBadMsg,
		`{"t":"auth","sig":"short"}`:                       CodeBadMsg,
		`{"t":"pair_confirm","sig":"short"}`:               CodeBadMsg,
		`{"t":"pair_ok","sig":"short"}`:                    CodeBadMsg,
		`{"t":"cmd","id":"1","action":"reboot","sig":"x"}`: CodeBadMsg,
		`{"t":"hello","v":1,"host":"h","os":"beos","agent_ver":"1","macs":[],"nonce":"0011223344556677"}`:                                                                                                     CodeBadMsg,
		`{"t":"hello","v":1,"host":"h","os":"linux","agent_ver":"","macs":[],"nonce":"0011223344556677"}`:                                                                                                     CodeBadMsg,
		`{"t":"hello","v":1,"host":"h","os":"linux","agent_ver":"1","macs":["AA:BB:CC:DD:EE:FF"],"nonce":"0011223344556677"}`:                                                                                 CodeBadMsg,
		`{"t":"hello","v":1,"host":"h","os":"linux","agent_ver":"1","macs":[],"nonce":"00"}`:                                                                                                                  CodeBadMsg,
		`{"t":"hello","v":1,"host":"` + strings.Repeat("h", 65) + `","os":"linux","agent_ver":"1","macs":[],"nonce":"0011223344556677"}`:                                                                      CodeBadMsg,
		`{"t":"hello","v":1,"host":"h","os":"linux","agent_ver":"1","macs":["00:00:00:00:00:01","00:00:00:00:00:02","00:00:00:00:00:03","00:00:00:00:00:04","00:00:00:00:00:05"],"nonce":"0011223344556677"}`: CodeBadMsg,
	}
	for line, code := range cases {
		_, err := Decode([]byte(line))
		if got := CodeOf(err); got != code {
			t.Errorf("Decode(%s) code = %q (%v), want %q", line, got, err, code)
		}
	}
}

func TestUnknownFieldsIgnored(t *testing.T) {
	m, err := Decode([]byte(`{"t":"ready","future":true}`))
	if err != nil || m.Type() != TypeReady {
		t.Fatalf("got %v %v", m, err)
	}
}

func TestEncodeTooLong(t *testing.T) {
	h := &Hello{V: 1, Host: strings.Repeat("x", 600), OS: "linux", AgentVer: "1", Nonce: "0011223344556677"}
	if _, err := Encode(h); CodeOf(err) != CodeTooLong {
		t.Fatalf("err = %v", err)
	}
	// An encoded message with HTML-ish characters is not escaped.
	b, err := Encode(&Hello{V: 1, Host: "a<b>&", OS: "linux", AgentVer: "1", MACs: []string{}, Nonce: "0011223344556677"})
	if err != nil || !bytes.Contains(b, []byte(`"a<b>&"`)) {
		t.Fatalf("%s %v", b, err)
	}
}

func TestLineReader(t *testing.T) {
	exact := strings.Repeat("a", MaxLine-1)    // 511 + \n = 512 -> ok
	crlfOver := strings.Repeat("b", MaxLine-1) // 511 + \r\n = 513 -> too long
	in := "\n" + exact + "\n" + crlfOver + "\r\n" + "x\r\n" + strings.Repeat("c", 3000) + "\n" + "tail"
	lr := NewLineReader(strings.NewReader(in))
	l, err := lr.ReadLine()
	if err != nil || string(l) != exact {
		t.Fatalf("exact line: len=%d err=%v", len(l), err)
	}
	if _, err := lr.ReadLine(); !errors.Is(err, ErrTooLong) {
		t.Fatalf("crlf over: %v", err)
	}
	if l, err := lr.ReadLine(); err != nil || string(l) != "x" {
		t.Fatalf("crlf strip: %q %v", l, err)
	}
	if _, err := lr.ReadLine(); !errors.Is(err, ErrTooLong) {
		t.Fatalf("huge: %v", err)
	}
	if _, err := lr.ReadLine(); err != io.EOF {
		t.Fatalf("partial at EOF: %v", err)
	}
}

func TestMiscHelpers(t *testing.T) {
	if ValidCode("12345") || ValidCode("12345a") || !ValidCode("000000") {
		t.Error("ValidCode")
	}
	if _, err := DerivePairKey("12", "0011223344556677", "0011223344556677"); err == nil {
		t.Error("short code accepted")
	}
	if _, err := DerivePairKey("123456", "zz", "0011223344556677"); err == nil {
		t.Error("bad na accepted")
	}
	if _, err := DerivePairKey("123456", "0011223344556677", "00"); err == nil {
		t.Error("bad nd accepted")
	}
	n, err := NewNonce()
	if err != nil || !ValidNonce(n) {
		t.Errorf("nonce %q %v", n, err)
	}
	if len(HKDF([]byte("k"), nil, nil, 80)) != 80 {
		t.Error("HKDF multi-block length")
	}
	if (&Error{Code: "x"}).Error() != "proto: x" || CodeOf(io.EOF) != "" {
		t.Error("Error formatting")
	}
}
