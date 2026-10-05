// Package scripts runs the user scripts defined in the agent configuration
// ([[scripts]] in agent.toml) when the dongle sends cmd{action:"script:<id>"}.
//
// Each script runs in the background, at most one instance per id, with its
// own timeout. On timeout (or when the agent stops) the whole process tree
// is killed: the process group on Unix, the tree via taskkill on Windows.
package scripts

import (
	"context"
	"errors"
	"log/slog"
	"os"
	"os/exec"
	"strings"
	"sync"
	"time"

	"github.com/microesp/agent/internal/proto"
)

// TailBytes is how much of the combined stdout/stderr is kept for the log.
const TailBytes = 2048

// WaitDelay bounds how long Run waits for the output pipes to close after
// the script exited or was killed (a detached grandchild may keep them open).
var WaitDelay = 5 * time.Second

// Errors returned by Acquire.
var (
	ErrUnknown = errors.New("scripts: unknown script")
	ErrBusy    = errors.New("scripts: script already running")
)

// Spec is one configured script.
type Spec struct {
	ID      string
	Label   string
	Command string // run with "sh -c" (Unix) or "cmd /C" (Windows)
	Timeout time.Duration
}

// Result describes a finished run.
type Result struct {
	ID       string
	ExitCode int // -1 if killed by a signal or not started
	Duration time.Duration
	Output   string // tail of the combined output (at most TailBytes)
	TimedOut bool
	DryRun   bool
	Err      error // nil when the script exited with status 0
}

// Runner starts scripts by id and enforces one running instance per id.
// A nil *Runner has no scripts.
type Runner struct {
	log    *slog.Logger
	dryRun bool
	specs  []Spec
	byID   map[string]Spec

	// OnDone, if set, is called after each run (tests, status).
	OnDone func(Result)

	mu      sync.Mutex
	running map[string]bool
	wg      sync.WaitGroup
}

// New returns a Runner for specs (already validated by config). With
// dryRun the scripts are logged but never executed.
func New(specs []Spec, dryRun bool, log *slog.Logger) *Runner {
	if log == nil {
		log = slog.Default()
	}
	r := &Runner{log: log, dryRun: dryRun, byID: map[string]Spec{}, running: map[string]bool{}}
	for _, s := range specs {
		r.specs = append(r.specs, s)
		r.byID[s.ID] = s
	}
	return r
}

// List returns the id/label pairs announced to the dongle, never nil.
func (r *Runner) List() []proto.ScriptInfo {
	out := []proto.ScriptInfo{}
	if r == nil {
		return out
	}
	for _, s := range r.specs {
		out = append(out, proto.ScriptInfo{ID: s.ID, Label: s.Label})
	}
	return out
}

// Has reports whether script id is configured.
func (r *Runner) Has(id string) bool {
	if r == nil {
		return false
	}
	_, ok := r.byID[id]
	return ok
}

// Job is a reserved run of a script: call Start to run it or Release to
// give the reservation back without running it.
type Job struct {
	r    *Runner
	spec Spec
	once sync.Once
}

// Acquire reserves script id. It fails with ErrUnknown if id is not
// configured and with ErrBusy if that script is still running.
func (r *Runner) Acquire(id string) (*Job, error) {
	if r == nil {
		return nil, ErrUnknown
	}
	spec, ok := r.byID[id]
	if !ok {
		return nil, ErrUnknown
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.running[id] {
		return nil, ErrBusy
	}
	r.running[id] = true
	r.wg.Add(1)
	return &Job{r: r, spec: spec}, nil
}

// Release frees the reservation without running the script.
func (j *Job) Release() {
	j.once.Do(func() {
		j.r.mu.Lock()
		delete(j.r.running, j.spec.ID)
		j.r.mu.Unlock()
		j.r.wg.Done()
	})
}

// Start runs the script in a new goroutine and returns immediately.
// Cancelling ctx kills the script like a timeout does.
func (j *Job) Start(ctx context.Context) {
	go func() {
		defer j.Release()
		r := j.r
		var res Result
		if r.dryRun {
			r.log.Warn("dry run: script not executed", "script", j.spec.ID, "command", j.spec.Command)
			res = Result{ID: j.spec.ID, ExitCode: -1, DryRun: true}
		} else {
			res = Run(ctx, j.spec, r.log)
			logResult(r.log, res)
		}
		if r.OnDone != nil {
			r.OnDone(res)
		}
	}()
}

// Running reports whether script id is running.
func (r *Runner) Running(id string) bool {
	if r == nil {
		return false
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.running[id]
}

// Wait waits up to timeout for all running scripts to finish and reports
// whether they did.
func (r *Runner) Wait(timeout time.Duration) bool {
	if r == nil {
		return true
	}
	done := make(chan struct{})
	go func() { r.wg.Wait(); close(done) }()
	t := time.NewTimer(timeout)
	defer t.Stop()
	select {
	case <-done:
		return true
	case <-t.C:
		return false
	}
}

func logResult(log *slog.Logger, res Result) {
	attrs := []any{"script", res.ID, "exit_code", res.ExitCode, "duration", res.Duration.Round(time.Millisecond), "output", res.Output}
	switch {
	case res.TimedOut:
		log.Warn("script timed out, process tree killed", attrs...)
	case res.Err != nil:
		log.Warn("script failed", append(attrs, "err", res.Err)...)
	default:
		log.Info("script finished", attrs...)
	}
}

// Run executes spec synchronously with its timeout and returns the result.
func Run(ctx context.Context, spec Spec, log *slog.Logger) Result {
	if log == nil {
		log = slog.Default()
	}
	res := Result{ID: spec.ID, ExitCode: -1}
	if spec.Timeout > 0 {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, spec.Timeout)
		defer cancel()
	}
	cmd := command(ctx, spec.Command)
	cmd.Env = append(os.Environ(), "MICROESP_SCRIPT_ID="+spec.ID)
	tail := &tailBuffer{max: TailBytes}
	cmd.Stdout, cmd.Stderr = tail, tail
	cmd.WaitDelay = WaitDelay

	start := time.Now()
	err := cmd.Start()
	if err != nil {
		res.Err = err
		log.Warn("script could not be started", "script", spec.ID, "err", err)
		return res
	}
	log.Info("script started", "script", spec.ID, "label", spec.Label, "pid", cmd.Process.Pid, "timeout", spec.Timeout)
	err = cmd.Wait()
	res.Duration = time.Since(start)
	res.Output = tail.String()
	if cmd.ProcessState != nil {
		res.ExitCode = cmd.ProcessState.ExitCode()
	}
	res.TimedOut = errors.Is(ctx.Err(), context.DeadlineExceeded)
	if errors.Is(err, exec.ErrWaitDelay) && res.ExitCode == 0 && ctx.Err() == nil {
		// Exited fine but a background child kept the output open.
		err = nil
	}
	res.Err = err
	return res
}

// tailBuffer keeps the last max bytes written to it.
type tailBuffer struct {
	mu        sync.Mutex
	max       int
	buf       []byte
	truncated bool
}

func (t *tailBuffer) Write(p []byte) (int, error) {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.buf = append(t.buf, p...)
	if over := len(t.buf) - t.max; over > 0 {
		t.buf = append(t.buf[:0], t.buf[over:]...)
		t.truncated = true
	}
	return len(p), nil
}

func (t *tailBuffer) String() string {
	t.mu.Lock()
	defer t.mu.Unlock()
	s := strings.ToValidUTF8(string(t.buf), "")
	s = strings.TrimRight(s, "\r\n")
	if t.truncated {
		s = "[...]" + s
	}
	return s
}
