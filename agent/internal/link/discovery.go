package link

import (
	"errors"
	"fmt"
	"io"
	"sort"
	"strings"

	"go.bug.st/serial"
	"go.bug.st/serial/enumerator"
)

// USB identity of the MicroESP dongle (cdc-v1 §1).
const (
	VID     = "303a"
	PID     = "4002"
	Product = "MicroESP"
)

// ErrNotFound means no MicroESP dongle is attached.
var ErrNotFound = errors.New("link: MicroESP device not found")

// PortInfo describes an enumerated serial port.
type PortInfo struct {
	Name    string
	IsUSB   bool
	VID     string
	PID     string
	Serial  string
	Product string
}

// Lister enumerates serial ports.
type Lister func() ([]PortInfo, error)

// Opener opens a serial port by path.
type Opener func(path string) (io.ReadWriteCloser, error)

// SerialLister enumerates ports with go.bug.st/serial/enumerator.
func SerialLister() ([]PortInfo, error) {
	ps, err := enumerator.GetDetailedPortsList()
	if err != nil {
		return nil, err
	}
	out := make([]PortInfo, 0, len(ps))
	for _, p := range ps {
		out = append(out, PortInfo{Name: p.Name, IsUSB: p.IsUSB, VID: p.VID, PID: p.PID, Serial: p.SerialNumber, Product: p.Product})
	}
	return out, nil
}

// SerialOpener opens path at 115200 8N1 (baudrate is irrelevant on CDC).
// DTR/RTS are asserted (library default) so the device sees the host connected.
func SerialOpener(path string) (io.ReadWriteCloser, error) {
	return serial.Open(path, &serial.Mode{BaudRate: 115200, DataBits: 8, Parity: serial.NoParity, StopBits: serial.OneStopBit})
}

// IsMicroESP reports whether p is a MicroESP dongle: Espressif VID 303a with
// PID 4002 or a product string containing "MicroESP".
func IsMicroESP(p PortInfo) bool {
	if !strings.EqualFold(p.VID, VID) {
		return false
	}
	return strings.EqualFold(p.PID, PID) || strings.Contains(strings.ToLower(p.Product), strings.ToLower(Product))
}

// Candidates returns the MicroESP ports, sorted by name.
func Candidates(list Lister) ([]PortInfo, error) {
	ps, err := list()
	if err != nil {
		return nil, fmt.Errorf("link: enumerate ports: %w", err)
	}
	var out []PortInfo
	for _, p := range ps {
		if IsMicroESP(p) {
			out = append(out, p)
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Name < out[j].Name })
	return out, nil
}

// Find resolves device ("auto" or a path) to a port path.
func Find(list Lister, device string) (string, error) {
	if device != "" && device != "auto" {
		return device, nil
	}
	cs, err := Candidates(list)
	if err != nil {
		return "", err
	}
	if len(cs) == 0 {
		return "", ErrNotFound
	}
	return cs[0].Name, nil
}
