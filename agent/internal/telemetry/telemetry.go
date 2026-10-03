// Package telemetry samples host metrics in protocol units (tenths of %).
package telemetry

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"math"
	"net"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"sync"
	"time"
	"unicode/utf8"

	"github.com/shirou/gopsutil/v4/cpu"
	"github.com/shirou/gopsutil/v4/disk"
	"github.com/shirou/gopsutil/v4/host"
	"github.com/shirou/gopsutil/v4/mem"
)

// Snapshot holds one sample. CPU, Mem and DiskFree are tenths of percent
// (0..1000); Uptime is seconds.
type Snapshot struct {
	CPU      int
	Mem      int
	DiskFree int
	Uptime   uint32
}

// Collector produces snapshots.
type Collector interface {
	Sample(ctx context.Context) (Snapshot, error)
}

// Tenths converts a percentage to clamped tenths of percent.
func Tenths(pct float64) int {
	if math.IsNaN(pct) || pct <= 0 {
		return 0
	}
	v := int(math.Round(pct * 10))
	if v > 1000 {
		return 1000
	}
	return v
}

// System is the gopsutil-backed Collector. CPU usage is measured over the
// window between two consecutive Sample calls.
type System struct {
	Disks []string
	Log   *slog.Logger

	// Indirections for tests.
	cpuPercent func(ctx context.Context) (float64, error)
	memPercent func(ctx context.Context) (float64, error)
	diskUsed   func(ctx context.Context, path string) (float64, error)
	uptime     func(ctx context.Context) (uint64, error)
}

// NewSystem returns a System collector for the given mount points.
func NewSystem(disks []string, log *slog.Logger) *System {
	return &System{
		Disks: disks,
		Log:   log,
		cpuPercent: func(ctx context.Context) (float64, error) {
			p, err := cpu.PercentWithContext(ctx, 0, false) // since previous call
			if err != nil || len(p) == 0 {
				return 0, errors.Join(err, errors.New("no cpu data"))
			}
			return p[0], nil
		},
		memPercent: func(ctx context.Context) (float64, error) {
			v, err := mem.VirtualMemoryWithContext(ctx)
			if err != nil {
				return 0, err
			}
			return v.UsedPercent, nil // used excludes cache/buffers
		},
		diskUsed: func(ctx context.Context, path string) (float64, error) {
			u, err := disk.UsageWithContext(ctx, path)
			if err != nil {
				return 0, err
			}
			return u.UsedPercent, nil // used/(used+avail), as df
		},
		uptime: host.UptimeWithContext,
	}
}

// Sample implements Collector.
func (s *System) Sample(ctx context.Context) (Snapshot, error) {
	var snap Snapshot
	c, err := s.cpuPercent(ctx)
	if err != nil {
		return snap, fmt.Errorf("cpu: %w", err)
	}
	m, err := s.memPercent(ctx)
	if err != nil {
		return snap, fmt.Errorf("mem: %w", err)
	}
	minFree, ok := 1000, false
	for _, d := range s.Disks {
		used, err := s.diskUsed(ctx, d)
		if err != nil {
			s.logger().Warn("disk usage unavailable", "path", d, "err", err)
			continue
		}
		ok = true
		if f := Tenths(100 - used); f < minFree {
			minFree = f
		}
	}
	if !ok {
		return snap, errors.New("disk: no configured disk could be read")
	}
	up, err := s.uptime(ctx)
	if err != nil {
		return snap, fmt.Errorf("uptime: %w", err)
	}
	if up > math.MaxUint32 {
		up = math.MaxUint32
	}
	return Snapshot{CPU: Tenths(c), Mem: Tenths(m), DiskFree: minFree, Uptime: uint32(up)}, nil
}

func (s *System) logger() *slog.Logger {
	if s.Log == nil {
		return slog.Default()
	}
	return s.Log
}

// Throttle decides when telemetry must be sent: every Interval, or as soon
// as any percentage changes by more than Delta tenths since the last send.
type Throttle struct {
	Interval time.Duration
	Delta    int

	last   Snapshot
	lastAt time.Time
	sent   bool
}

// DefaultDelta is the "cambio >20 décimas" threshold.
const DefaultDelta = 20

// Should reports whether s must be sent at now.
func (t *Throttle) Should(s Snapshot, now time.Time) bool {
	if !t.sent || now.Sub(t.lastAt) >= t.Interval {
		return true
	}
	return abs(s.CPU-t.last.CPU) > t.Delta || abs(s.Mem-t.last.Mem) > t.Delta ||
		abs(s.DiskFree-t.last.DiskFree) > t.Delta
}

// Mark records that s was sent at now.
func (t *Throttle) Mark(s Snapshot, now time.Time) {
	t.last, t.lastAt, t.sent = s, now, true
}

func abs(v int) int {
	if v < 0 {
		return -v
	}
	return v
}

// HostInfo is sent in hello.
type HostInfo struct {
	Hostname string
	MACs     []string
}

// MaxHost is the hello.host limit in bytes.
const MaxHost = 64

// TruncateHost cuts h to at most MaxHost bytes on a rune boundary.
func TruncateHost(h string) string {
	if len(h) <= MaxHost {
		return h
	}
	h = h[:MaxHost]
	for !utf8.ValidString(h) {
		h = h[:len(h)-1]
	}
	return h
}

// ReadHostInfo returns the hostname and up to 4 physical NIC MACs.
func ReadHostInfo() (HostInfo, error) {
	name, err := os.Hostname()
	if err != nil {
		return HostInfo{}, err
	}
	ifs, err := net.Interfaces()
	if err != nil {
		return HostInfo{Hostname: TruncateHost(name)}, err
	}
	return HostInfo{Hostname: TruncateHost(name), MACs: PhysicalMACs(ifs, sysfsPhysical("/sys/class/net"))}, nil
}

// virtualPrefixes are interface name prefixes that never carry WOL.
// "lo" is matched exactly (see PhysicalMACs) so Windows NICs named
// "Local Area Connection" are not dropped.
var virtualPrefixes = []string{"docker", "br-", "veth", "virbr", "tailscale",
	"vnet", "tun", "tap", "wg", "zt", "vboxnet", "vmnet", "cni", "flannel", "podman", "lxc", "lxd"}

// sysfsPhysical reports whether an interface is backed by a device
// (/sys/class/net/<if>/device). Without sysfs (Windows) everything passes.
func sysfsPhysical(root string) func(string) bool {
	if runtime.GOOS != "linux" {
		return func(string) bool { return true }
	}
	if _, err := os.Stat(root); err != nil {
		return func(string) bool { return true }
	}
	return func(name string) bool {
		_, err := os.Stat(filepath.Join(root, name, "device"))
		return err == nil
	}
}

// PhysicalMACs filters interfaces to physical NICs (wired and wireless),
// returning at most 4 lowercase MACs, up interfaces first.
func PhysicalMACs(ifs []net.Interface, isPhysical func(string) bool) []string {
	type cand struct {
		mac  string
		up   bool
		name string
	}
	var cs []cand
	seen := map[string]bool{}
next:
	for _, i := range ifs {
		if i.Flags&net.FlagLoopback != 0 || len(i.HardwareAddr) != 6 {
			continue
		}
		lname := strings.ToLower(i.Name)
		if lname == "lo" {
			continue
		}
		for _, p := range virtualPrefixes {
			if strings.HasPrefix(lname, p) {
				continue next
			}
		}
		if isPhysical != nil && !isPhysical(i.Name) {
			continue
		}
		mac := strings.ToLower(i.HardwareAddr.String())
		if mac == "00:00:00:00:00:00" || seen[mac] {
			continue
		}
		seen[mac] = true
		cs = append(cs, cand{mac, i.Flags&net.FlagUp != 0, i.Name})
	}
	sort.SliceStable(cs, func(a, b int) bool {
		if cs[a].up != cs[b].up {
			return cs[a].up
		}
		return cs[a].name < cs[b].name
	})
	out := []string{}
	for _, c := range cs {
		if len(out) == 4 {
			break
		}
		out = append(out, c.mac)
	}
	return out
}

// Fake is a Collector for tests.
type Fake struct {
	mu    sync.Mutex
	snap  Snapshot
	err   error
	calls int
}

// Set changes the snapshot returned next.
func (f *Fake) Set(s Snapshot, err error) {
	f.mu.Lock()
	f.snap, f.err = s, err
	f.mu.Unlock()
}

// Calls returns how many times Sample was called.
func (f *Fake) Calls() int {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.calls
}

// Sample implements Collector.
func (f *Fake) Sample(context.Context) (Snapshot, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.calls++
	return f.snap, f.err
}
