// Package proto implements the MicroESP CDC v1 wire protocol
// (docs/protocol/cdc-v1.md): message types, JSON-lines framing,
// validation and the HMAC/HKDF primitives.
package proto

// Version is the only protocol version supported.
const Version = 1

// Message type identifiers (field "t").
const (
	TypeHello       = "hello"
	TypeWelcome     = "welcome"
	TypeAuth        = "auth"
	TypeReady       = "ready"
	TypeTele        = "tele"
	TypeHB          = "hb"
	TypeAck         = "ack"
	TypeNotice      = "notice"
	TypeCmd         = "cmd"
	TypeErr         = "err"
	TypePair        = "pair"
	TypePairChal    = "pair_chal"
	TypePairConfirm = "pair_confirm"
	TypePairOK      = "pair_ok"
)

// Command / notice actions.
const (
	ActionShutdown = "shutdown"
	ActionReboot   = "reboot"
	ActionCancel   = "cancel" // only valid in notice
)

// Error codes carried by err{code} (dongle -> agent) and ack{err}.
const (
	CodeBadMsg             = "bad_msg"
	CodeTooLong            = "too_long"
	CodeUnauth             = "unauth"
	CodeNotPaired          = "not_paired"
	CodeUnsupportedVersion = "unsupported_version"
	CodePairFailed         = "pair_failed"

	AckBadSig        = "bad_sig"
	AckReplay        = "replay"
	AckExecFailed    = "exec_failed"
	AckUnknownAction = "unknown_action"
)

// Limits.
const (
	MaxLine     = 512 // bytes per line including terminator
	MaxHostLen  = 64
	MaxMACs     = 4
	MaxTenths   = 1000
	NonceBytes  = 8
	NonceHexLen = 2 * NonceBytes
	SigHexLen   = 64
	KeyBytes    = 32
)

// Message is implemented by every protocol message.
type Message interface {
	Type() string
	head() *Head
}

// Head carries the "t" discriminator; embedded in every message.
type Head struct {
	T string `json:"t"`
}

func (h *Head) head() *Head { return h }

// Hello starts a session (agent -> dongle).
type Hello struct {
	Head
	V        int      `json:"v"`
	Host     string   `json:"host"`
	OS       string   `json:"os"`
	AgentVer string   `json:"agent_ver"`
	MACs     []string `json:"macs"`
	Nonce    string   `json:"nonce"`
}

// Welcome answers hello (dongle -> agent).
type Welcome struct {
	Head
	V     int    `json:"v"`
	FW    string `json:"fw"`
	Dev   string `json:"dev"`
	Nonce string `json:"nonce"`
	Sig   string `json:"sig"`
}

// Auth proves key possession (agent -> dongle).
type Auth struct {
	Head
	Sig string `json:"sig"`
}

// Ready marks an authenticated session (dongle -> agent).
type Ready struct{ Head }

// Tele carries telemetry in tenths of percent (agent -> dongle).
type Tele struct {
	Head
	Seq      uint32 `json:"seq"`
	CPU      int    `json:"cpu"`
	Mem      int    `json:"mem"`
	DiskFree int    `json:"disk_free"`
	Uptime   uint32 `json:"uptime"`
}

// HB is a heartbeat (agent -> dongle).
type HB struct{ Head }

// Ack answers a cmd (agent -> dongle).
type Ack struct {
	Head
	ID  uint32 `json:"id"`
	OK  bool   `json:"ok"`
	Err string `json:"err,omitempty"`
}

// Notice announces a countdown (dongle -> agent).
type Notice struct {
	Head
	Action string `json:"action"`
	In     uint32 `json:"in"`
}

// Cmd orders a power action (dongle -> agent).
type Cmd struct {
	Head
	ID     uint32 `json:"id"`
	Action string `json:"action"`
	Sig    string `json:"sig"`
}

// Err reports a protocol error (dongle -> agent).
type Err struct {
	Head
	Code string `json:"code"`
}

// Pair starts pairing (agent -> dongle).
type Pair struct {
	Head
	V     int    `json:"v"`
	Nonce string `json:"nonce"`
}

// PairChal is the dongle's pairing challenge.
type PairChal struct {
	Head
	Nonce string `json:"nonce"`
}

// PairConfirm proves knowledge of the code (agent -> dongle).
type PairConfirm struct {
	Head
	Sig string `json:"sig"`
}

// PairOK proves the dongle derived the same key.
type PairOK struct {
	Head
	Sig string `json:"sig"`
}

func (*Hello) Type() string       { return TypeHello }
func (*Welcome) Type() string     { return TypeWelcome }
func (*Auth) Type() string        { return TypeAuth }
func (*Ready) Type() string       { return TypeReady }
func (*Tele) Type() string        { return TypeTele }
func (*HB) Type() string          { return TypeHB }
func (*Ack) Type() string         { return TypeAck }
func (*Notice) Type() string      { return TypeNotice }
func (*Cmd) Type() string         { return TypeCmd }
func (*Err) Type() string         { return TypeErr }
func (*Pair) Type() string        { return TypePair }
func (*PairChal) Type() string    { return TypePairChal }
func (*PairConfirm) Type() string { return TypePairConfirm }
func (*PairOK) Type() string      { return TypePairOK }

// newByType returns an empty message for a type, or nil if unknown.
func newByType(t string) Message {
	switch t {
	case TypeHello:
		return &Hello{}
	case TypeWelcome:
		return &Welcome{}
	case TypeAuth:
		return &Auth{}
	case TypeReady:
		return &Ready{}
	case TypeTele:
		return &Tele{}
	case TypeHB:
		return &HB{}
	case TypeAck:
		return &Ack{}
	case TypeNotice:
		return &Notice{}
	case TypeCmd:
		return &Cmd{}
	case TypeErr:
		return &Err{}
	case TypePair:
		return &Pair{}
	case TypePairChal:
		return &PairChal{}
	case TypePairConfirm:
		return &PairConfirm{}
	case TypePairOK:
		return &PairOK{}
	}
	return nil
}

// requiredFields lists the keys that must be present per type.
var requiredFields = map[string][]string{
	TypeHello:       {"v", "host", "os", "agent_ver", "macs", "nonce"},
	TypeWelcome:     {"v", "fw", "dev", "nonce", "sig"},
	TypeAuth:        {"sig"},
	TypeTele:        {"seq", "cpu", "mem", "disk_free", "uptime"},
	TypeAck:         {"id", "ok"},
	TypeNotice:      {"action", "in"},
	TypeCmd:         {"id", "action", "sig"},
	TypeErr:         {"code"},
	TypePair:        {"v", "nonce"},
	TypePairChal:    {"nonce"},
	TypePairConfirm: {"sig"},
	TypePairOK:      {"sig"},
}
