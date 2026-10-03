package proto

import (
	"crypto/hmac"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"strconv"
)

// PairInfo is the HKDF info string for pairing.
const PairInfo = "microesp-pair-v1"

// Sign returns hex(HMAC-SHA256(key, msg)) in lowercase.
func Sign(key []byte, msg string) string {
	m := hmac.New(sha256.New, key)
	m.Write([]byte(msg))
	return hex.EncodeToString(m.Sum(nil))
}

// Verify compares sig against Sign(key, msg) in constant time.
func Verify(key []byte, msg, sig string) bool {
	return hmac.Equal([]byte(Sign(key, msg)), []byte(sig))
}

// Signed message builders (§3, §4). Nonces are their hex strings.

func WelcomeMsg(na, nd string) string { return "welcome|" + na + "|" + nd }
func AuthMsg(na, nd string) string    { return "auth|" + nd + "|" + na }
func CmdMsg(id uint32, action, na, nd string) string {
	return "cmd|" + strconv.FormatUint(uint64(id), 10) + "|" + action + "|" + na + "|" + nd
}
func PairMsg(na, nd string) string   { return "pair|" + na + "|" + nd }
func PairOKMsg(na, nd string) string { return "pair_ok|" + nd + "|" + na }

// HKDF implements RFC 5869 HKDF-SHA256.
func HKDF(ikm, salt, info []byte, length int) []byte {
	ext := hmac.New(sha256.New, salt)
	ext.Write(ikm)
	prk := ext.Sum(nil)
	var out, prev []byte
	for i := byte(1); len(out) < length; i++ {
		m := hmac.New(sha256.New, prk)
		m.Write(prev)
		m.Write(info)
		m.Write([]byte{i})
		prev = m.Sum(nil)
		out = append(out, prev...)
	}
	return out[:length]
}

// ValidCode reports whether code is exactly 6 ASCII digits.
func ValidCode(code string) bool {
	if len(code) != 6 {
		return false
	}
	for i := 0; i < len(code); i++ {
		if code[i] < '0' || code[i] > '9' {
			return false
		}
	}
	return true
}

// DerivePairKey computes K = HKDF-SHA256(code, bytes(Na)||bytes(Nd), PairInfo, 32).
func DerivePairKey(code, na, nd string) ([]byte, error) {
	if !ValidCode(code) {
		return nil, errors.New("pairing code must be 6 digits")
	}
	a, err := hex.DecodeString(na)
	if err != nil || len(a) != NonceBytes {
		return nil, fmt.Errorf("invalid agent nonce %q", na)
	}
	d, err := hex.DecodeString(nd)
	if err != nil || len(d) != NonceBytes {
		return nil, fmt.Errorf("invalid dongle nonce %q", nd)
	}
	return HKDF([]byte(code), append(a, d...), []byte(PairInfo), KeyBytes), nil
}

// NewNonce returns 8 random bytes as 16 lowercase hex chars.
func NewNonce() (string, error) {
	b := make([]byte, NonceBytes)
	if _, err := rand.Read(b); err != nil {
		return "", err
	}
	return hex.EncodeToString(b), nil
}
