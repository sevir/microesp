//go:build linux

package main

import (
	"log/slog"
	"os/user"
	"strconv"

	"github.com/microesp/agent/internal/config"
	"github.com/microesp/agent/internal/scripts"
)

// agentUser is the account of the agent service, the only non-root peer
// the scripts runner accepts.
const agentUser = "microesp"

// lookupUID abstracts user lookups for tests.
var lookupUID = func(name string) (uint32, bool) {
	u, err := user.Lookup(name)
	if err != nil {
		return 0, false
	}
	uid, err := strconv.ParseUint(u.Uid, 10, 32)
	if err != nil {
		return 0, false
	}
	return uint32(uid), true
}

// cmdScriptsRunner serves start requests from the agent on a unix socket
// (systemd socket activation, or --listen) and runs the user scripts as the
// user this process runs as.
func cmdScriptsRunner(d deps, cfg config.Config, listen string, log *slog.Logger) int {
	ln, activated, err := scripts.Listen(listen)
	if err != nil {
		log.Error("scripts runner: cannot listen", "err", err)
		return 1
	}
	uid, ok := lookupUID(agentUser)
	if !ok {
		log.Warn("scripts runner: user not found, only root may send requests", "user", agentUser)
	}
	r := newScripts(cfg, log)
	if r == nil {
		log.Warn("scripts runner: no [[scripts]] configured; every request will be answered unknown")
	}
	log.Info("scripts runner starting", "version", version, "socket_activated", activated, "listen", ln.Addr().String(), "scripts", scriptIDs(cfg), "dry_run", cfg.DryRun)
	ctx, cancel := d.ctx()
	defer cancel()
	srv := &scripts.Server{Runner: r, Log: log, AllowUID: scripts.AllowAgent(uid, ok)}
	err = srv.Serve(ctx, ln)
	cancel() // kills the scripts still running
	if !r.Wait(scriptsStopWait) {
		log.Warn("user scripts still running at exit")
	}
	if err != nil {
		log.Error("scripts runner stopped", "err", err)
		return 1
	}
	log.Info("scripts runner stopped")
	return 0
}
