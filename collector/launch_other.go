//go:build !windows

package main

import (
	"context"
	"os"
)

func launch(ctx context.Context, args []string) error {
	return run(ctx, args, os.Stdout, os.Stderr)
}
