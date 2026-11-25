package main

import (
	"context"
	_ "embed"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
)

//go:embed config.example.yaml
var exampleConfig []byte

func createFile(path string, data []byte) error {
	if err := os.MkdirAll(filepath.Dir(path), 0700); err != nil {
		return err
	}
	f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0600)
	if err != nil {
		return err
	}
	_, writeErr := f.Write(data)
	closeErr := f.Close()
	if err := errors.Join(writeErr, closeErr); err != nil {
		os.Remove(path)
		return err
	}
	return nil
}

func checkConfig(cfg config) error {
	if err := cfg.readIdentity(); err != nil {
		return err
	}
	if err := cfg.loadToken(); err != nil {
		return err
	}
	p, err := newPoster(cfg)
	if err == nil {
		p.close()
	}
	return err
}

func run(ctx context.Context, args []string, stdout, stderr io.Writer) error {
	if len(args) > 0 && args[0] == "run" {
		args = args[1:]
	} else if len(args) > 0 && !strings.HasPrefix(args[0], "-") {
		command := args[0]
		if command != "init" && command != "check" && command != "export" {
			return fmt.Errorf("unknown command %q", command)
		}
		cfg, err := configFlags(args[1:], "curio-collector "+command, false, stderr)
		if err != nil {
			return err
		}
		if command == "init" {
			data := exampleConfig
			if cfg.VerifyTLS {
				data = append([]byte("verify_tls: true\n"), data...)
			}
			if err := createFile(cfg.ConfigFile, data); err != nil {
				return err
			}
			fmt.Fprintln(stdout, cfg.ConfigFile)
			return nil
		}
		if command == "export" {
			return exportConfig(cfg.ConfigFile, stdout, cfg.VerifyTLS)
		}
		cfg, err = loadLocalConfig(cfg)
		if err != nil {
			return err
		}
		if err := checkConfig(cfg); err != nil {
			return err
		}
		fmt.Fprintln(stdout, "Configuration OK")
		return nil
	}
	cfg, err := parseConfig(args, stderr)
	if err != nil {
		return err
	}
	if err := cfg.prepareIdentity(); err != nil {
		return err
	}
	return runCollector(ctx, cfg, stdout, stderr)
}

func main() {
	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer cancel()
	if err := launch(ctx, os.Args[1:]); err != nil {
		if errors.Is(err, flag.ErrHelp) {
			return
		}
		fmt.Fprintln(os.Stderr, "collector:", err)
		os.Exit(1)
	}
}
