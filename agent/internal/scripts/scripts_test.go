//go:build !windows

package scripts

// The tests run harmless shell commands only (exit, echo, sleep).

import (
	"context"
	"errors"
	"io"
	"log/slog"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"testing"
	"time"
)

var quiet = slog.New(slog.NewTextHandler(io.Discard, nil))

func TestRunExitCodeAndOutput(t *testing.T) {
	res := Run(context.Background(), Spec{ID: "x", Command: `echo out; echo err >&2; echo "id=$MICROESP_SCRIPT_ID"; exit 3`, Timeout: 5 * time.Second}, quiet)
	if res.ExitCode != 3 || res.Err == nil || res.TimedOut {
		t.Fatalf("%+v", res)
	}
	for _, want := range []string{"out", "err", "id=x"} {
		if !strings.Contains(res.Output, want) {
			t.Errorf("output %q lacks %q", res.Output, want)
		}
	}
	ok := Run(context.Background(), Spec{ID: "ok", Command: "true", Timeout: 5 * time.Second}, nil)
	if ok.ExitCode != 0 || ok.Err != nil || ok.Output != "" {
		t.Fatalf("%+v", ok)
	}
}

func TestRunOutputTail(t *testing.T) {
	res := Run(context.Background(), Spec{ID: "big", Command: `i=0; while [ $i -lt 500 ]; do echo "line $i"; i=$((i+1)); done; echo LAST`, Timeout: 5 * time.Second}, quiet)
	if !strings.HasPrefix(res.Output, "[...]") || !strings.HasSuffix(res.Output, "LAST") || len(res.Output) > TailBytes+5 {
		t.Fatalf("tail len=%d %q...", len(res.Output), res.Output[:20])
	}
}

func TestRunTimeoutKillsProcessGroup(t *testing.T) {
	pidFile := filepath.Join(t.TempDir(), "child.pid")
	start := time.Now()
	// The background child would outlive sh if only sh were killed.
	res := Run(context.Background(), Spec{ID: "slow", Command: "sleep 30 & echo $! > " + pidFile + "; wait", Timeout: 200 * time.Millisecond}, quiet)
	if !res.TimedOut || res.Err == nil || res.ExitCode != -1 {
		t.Fatalf("%+v", res)
	}
	if time.Since(start) > 3*time.Second {
		t.Fatalf("timeout took %v", time.Since(start))
	}
	b, err := os.ReadFile(pidFile)
	if err != nil {
		t.Fatal(err)
	}
	pid, _ := strconv.Atoi(strings.TrimSpace(string(b)))
	deadline := time.Now().Add(2 * time.Second)
	for syscall.Kill(pid, 0) == nil {
		if time.Now().After(deadline) {
			_ = syscall.Kill(pid, syscall.SIGKILL)
			t.Fatalf("child %d survived the timeout", pid)
		}
		time.Sleep(10 * time.Millisecond)
	}
}

func TestRunStartError(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	res := Run(ctx, Spec{ID: "c", Command: "true", Timeout: time.Second}, quiet)
	if res.Err == nil || res.ExitCode != -1 {
		t.Fatalf("%+v", res)
	}
}

func TestRunnerConcurrencyAndUnknown(t *testing.T) {
	r := New([]Spec{
		{ID: "slow", Label: "Slow", Command: "sleep 0.3", Timeout: 5 * time.Second},
		{ID: "fast", Label: "Fast", Command: "exit 0", Timeout: 5 * time.Second},
	}, false, quiet)
	done := make(chan Result, 4)
	r.OnDone = func(res Result) { done <- res }

	if l := r.List(); len(l) != 2 || l[0].ID != "slow" || l[1].Label != "Fast" {
		t.Fatalf("list %+v", l)
	}
	if _, err := r.Acquire("nope"); !errors.Is(err, ErrUnknown) {
		t.Fatalf("unknown: %v", err)
	}
	j, err := r.Acquire("slow")
	if err != nil {
		t.Fatal(err)
	}
	j.Start(context.Background())
	if _, err := r.Acquire("slow"); !errors.Is(err, ErrBusy) {
		t.Fatalf("second slow: %v", err)
	}
	if !r.Running("slow") {
		t.Fatal("slow not running")
	}
	// Another script may run meanwhile.
	f, err := r.Acquire("fast")
	if err != nil {
		t.Fatal(err)
	}
	f.Start(context.Background())
	got := map[string]Result{}
	for i := 0; i < 2; i++ {
		select {
		case res := <-done:
			got[res.ID] = res
		case <-time.After(3 * time.Second):
			t.Fatal("timeout waiting for scripts")
		}
	}
	if got["slow"].Err != nil || got["fast"].Err != nil || got["slow"].Duration < 250*time.Millisecond {
		t.Fatalf("%+v", got)
	}
	if !r.Wait(time.Second) || r.Running("slow") {
		t.Fatal("still running")
	}
	// Once finished it can run again; Release without Start frees it too.
	j, err = r.Acquire("slow")
	if err != nil {
		t.Fatal(err)
	}
	j.Release()
	j.Release()
	if _, err := r.Acquire("slow"); err != nil {
		t.Fatal(err)
	}
}

func TestRunnerCancelKills(t *testing.T) {
	r := New([]Spec{{ID: "long", Command: "sleep 30", Timeout: time.Minute}}, false, quiet)
	done := make(chan Result, 1)
	r.OnDone = func(res Result) { done <- res }
	ctx, cancel := context.WithCancel(context.Background())
	j, _ := r.Acquire("long")
	j.Start(ctx)
	time.Sleep(50 * time.Millisecond)
	cancel()
	if !r.Wait(3 * time.Second) {
		t.Fatal("script not killed on cancel")
	}
	if res := <-done; res.Err == nil || res.TimedOut {
		t.Fatalf("%+v", res)
	}
}

func TestRunnerDryRun(t *testing.T) {
	marker := filepath.Join(t.TempDir(), "ran")
	r := New([]Spec{{ID: "d", Command: "touch " + marker, Timeout: time.Second}}, true, quiet)
	done := make(chan Result, 1)
	r.OnDone = func(res Result) { done <- res }
	j, err := r.Acquire("d")
	if err != nil {
		t.Fatal(err)
	}
	j.Start(context.Background())
	if res := <-done; !res.DryRun {
		t.Fatalf("%+v", res)
	}
	if _, err := os.Stat(marker); err == nil {
		t.Fatal("dry run executed the command")
	}
}

func TestNilRunner(t *testing.T) {
	var r *Runner
	if l := r.List(); l == nil || len(l) != 0 {
		t.Fatal("nil list")
	}
	if _, err := r.Acquire("x"); !errors.Is(err, ErrUnknown) {
		t.Fatal(err)
	}
	if r.Running("x") || !r.Wait(0) {
		t.Fatal("nil runner state")
	}
}
