//go:build windows

package main

import (
	"context"

	"github.com/kardianos/service"
)

// ServiceName is the Windows service name used by install.ps1.
const ServiceName = "MicroESPAgent"

type program struct {
	fn     func(context.Context) error
	ctx    context.Context
	cancel context.CancelFunc
	done   chan error
}

func (p *program) Start(service.Service) error {
	go func() { p.done <- p.fn(p.ctx) }()
	return nil
}

func (p *program) Stop(service.Service) error {
	p.cancel()
	<-p.done
	return nil
}

// runService runs fn under the Windows Service Control Manager when
// started as a service, or in the foreground when interactive.
func runService(ctx context.Context, fn func(context.Context) error) error {
	if service.Interactive() {
		return fn(ctx)
	}
	cctx, cancel := context.WithCancel(ctx)
	defer cancel()
	p := &program{fn: fn, ctx: cctx, cancel: cancel, done: make(chan error, 1)}
	s, err := service.New(p, &service.Config{
		Name:        ServiceName,
		DisplayName: "MicroESP Agent",
		Description: "Link to the MicroESP USB dongle (telemetry and remote shutdown)",
	})
	if err != nil {
		return err
	}
	return s.Run()
}
