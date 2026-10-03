package telemetry

import (
	"context"
	"errors"
	"io"
	"log/slog"
	"math"
	"net"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"
)

var quiet = slog.New(slog.NewTextHandler(io.Discard, nil))

func TestTenths(t *testing.T) {
	for in, want := range map[float64]int{-3: 0, 0: 0, 12.34: 123, 12.36: 124, 99.99: 1000, 150: 1000, math.NaN(): 0} {
		if got := Tenths(in); got != want {
			t.Errorf("Tenths(%v) = %d want %d", in, got, want)
		}
	}
}

func fakeSystem(disks map[string]float64) *System {
	s := NewSystem(nil, quiet)
	for d := range disks {
		s.Disks = append(s.Disks, d)
	}
	s.cpuPercent = func(context.Context) (float64, error) { return 12.3, nil }
	s.memPercent = func(context.Context) (float64, error) { return 45.6, nil }
	s.diskUsed = func(_ context.Context, p string) (float64, error) {
		u, ok := disks[p]
		if !ok {
			return 0, errors.New("no such fs")
		}
		return u, nil
	}
	s.uptime = func(context.Context) (uint64, error) { return 3600, nil }
	return s
}

func TestSystemSample(t *testing.T) {
	s := fakeSystem(map[string]float64{"/": 40, "/home": 90.5})
	s.Disks = append(s.Disks, "/missing")
	snap, err := s.Sample(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	want := Snapshot{CPU: 123, Mem: 456, DiskFree: 95, Uptime: 3600}
	if snap != want {
		t.Fatalf("got %+v want %+v", snap, want)
	}
	s.uptime = func(context.Context) (uint64, error) { return math.MaxUint64, nil }
	if snap, _ := s.Sample(context.Background()); snap.Uptime != math.MaxUint32 {
		t.Error("uptime not clamped")
	}
}

func TestSystemSampleErrors(t *testing.T) {
	boom := errors.New("boom")
	ctx := context.Background()
	s := fakeSystem(map[string]float64{"/": 1})
	s.cpuPercent = func(context.Context) (float64, error) { return 0, boom }
	if _, err := s.Sample(ctx); !errors.Is(err, boom) {
		t.Error("cpu error not propagated")
	}
	s = fakeSystem(map[string]float64{"/": 1})
	s.memPercent = func(context.Context) (float64, error) { return 0, boom }
	if _, err := s.Sample(ctx); !errors.Is(err, boom) {
		t.Error("mem error")
	}
	s = fakeSystem(map[string]float64{"/": 1})
	s.uptime = func(context.Context) (uint64, error) { return 0, boom }
	if _, err := s.Sample(ctx); !errors.Is(err, boom) {
		t.Error("uptime error")
	}
	s = fakeSystem(nil)
	s.Disks = []string{"/nope"}
	if _, err := s.Sample(ctx); err == nil {
		t.Error("no disks readable accepted")
	}
	(&System{}).logger()
}

func TestRealSystemSample(t *testing.T) {
	// Reads real (harmless) metrics from this host.
	s := NewSystem([]string{"/"}, quiet)
	if runtime.GOOS == "windows" {
		s.Disks = []string{`C:\`}
	}
	snap, err := s.Sample(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if snap.Mem <= 0 || snap.Mem > 1000 || snap.DiskFree < 0 || snap.DiskFree > 1000 || snap.Uptime == 0 {
		t.Errorf("implausible %+v", snap)
	}
	hi, err := ReadHostInfo()
	if err != nil || hi.Hostname == "" || len(hi.MACs) > 4 {
		t.Errorf("host info %+v %v", hi, err)
	}
}

func TestThrottle(t *testing.T) {
	th := &Throttle{Interval: 10 * time.Second, Delta: DefaultDelta}
	t0 := time.Unix(1000, 0)
	s := Snapshot{CPU: 100, Mem: 100, DiskFree: 100}
	if !th.Should(s, t0) {
		t.Fatal("first sample not sent")
	}
	th.Mark(s, t0)
	if th.Should(Snapshot{CPU: 120, Mem: 80, DiskFree: 100}, t0.Add(time.Second)) {
		t.Error("change of exactly 20 must not trigger")
	}
	for _, c := range []Snapshot{{CPU: 121, Mem: 100, DiskFree: 100}, {CPU: 100, Mem: 79, DiskFree: 100}, {CPU: 100, Mem: 100, DiskFree: 130}} {
		if !th.Should(c, t0.Add(time.Second)) {
			t.Errorf("change %+v not sent", c)
		}
	}
	if !th.Should(s, t0.Add(10*time.Second)) {
		t.Error("interval elapsed not sent")
	}
}

func TestTruncateHost(t *testing.T) {
	if TruncateHost("pc") != "pc" {
		t.Error()
	}
	h := strings.Repeat("a", 63) + "ñb"
	got := TruncateHost(h)
	if len(got) != 63 {
		t.Errorf("len %d", len(got))
	}
}

func iface(name, mac string, flags net.Flags) net.Interface {
	hw, _ := net.ParseMAC(mac)
	return net.Interface{Name: name, HardwareAddr: hw, Flags: flags}
}

func TestPhysicalMACs(t *testing.T) {
	up := net.FlagUp
	ifs := []net.Interface{
		iface("lo", "00:00:00:00:00:00", net.FlagLoopback|up),
		iface("docker0", "02:42:ac:11:00:01", up),
		iface("br-1234", "02:42:ac:11:00:02", up),
		iface("veth9", "02:42:ac:11:00:03", up),
		iface("virbr0", "52:54:00:00:00:01", up),
		iface("tailscale0", "aa:aa:aa:aa:aa:aa", up),
		iface("vEthernet (WSL)", "00:15:5d:00:00:01", up),
		iface("wlp3s0", "AA:BB:CC:00:00:02", 0),
		iface("enp0s31f6", "FC:9D:05:18:EE:32", up),
		iface("eno2", "fc:9d:05:18:ee:32", up), // duplicate
		iface("dummy0", "11:22:33:44:55:66", up),
		iface("ib0", "00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:00:01", up),
		iface("e1", "00:00:00:00:00:00", up),
		iface("e2", "00:00:00:00:00:a2", up),
		iface("e3", "00:00:00:00:00:a3", up),
		iface("e4", "00:00:00:00:00:a4", up),
	}
	notDummy := func(n string) bool { return n != "dummy0" }
	got := PhysicalMACs(ifs, notDummy)
	want := "00:00:00:00:00:a2,00:00:00:00:00:a3,00:00:00:00:00:a4,fc:9d:05:18:ee:32"
	if strings.Join(got, ",") != want {
		t.Fatalf("got %v", got)
	}
	if got := PhysicalMACs(ifs[7:9], nil); strings.Join(got, ",") != "fc:9d:05:18:ee:32,aa:bb:cc:00:00:02" {
		t.Errorf("up-first ordering: %v", got)
	}
	// Regression: Windows wired NICs are often named "Local Area Connection".
	if got := PhysicalMACs([]net.Interface{iface("Local Area Connection", "fc:9d:05:18:ee:33", up)}, nil); len(got) != 1 {
		t.Errorf("Local Area Connection dropped: %v", got)
	}
	if got := PhysicalMACs(nil, nil); got == nil || len(got) != 0 {
		t.Error("expected empty non-nil slice")
	}
}

func TestSysfsPhysical(t *testing.T) {
	root := t.TempDir()
	if err := os.MkdirAll(filepath.Join(root, "eth0", "device"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(root, "dummy0"), 0o755); err != nil {
		t.Fatal(err)
	}
	f := sysfsPhysical(root)
	if runtime.GOOS == "linux" && (!f("eth0") || f("dummy0")) {
		t.Error("sysfs detection")
	}
	if !sysfsPhysical(filepath.Join(root, "missing"))("x") {
		t.Error("missing sysfs must allow all")
	}
}

func TestFake(t *testing.T) {
	f := &Fake{}
	f.Set(Snapshot{CPU: 5}, nil)
	s, err := f.Sample(context.Background())
	if err != nil || s.CPU != 5 || f.Calls() != 1 {
		t.Fatal(s, err)
	}
}
