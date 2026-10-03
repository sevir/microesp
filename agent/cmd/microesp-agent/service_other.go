//go:build !windows

package main

import "context"

// runService runs fn in the foreground; systemd supervises the process.
func runService(ctx context.Context, fn func(context.Context) error) error {
	return fn(ctx)
}
