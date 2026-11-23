package main

import (
	"crypto/rand"
	"encoding/hex"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
	"time"
)

type config struct {
	Local                                                          *localConfig
	ConfigFile                                                     string
	Endpoint, TokenFile, Token, CA, HostID, HostName, IdentityFile string
	Interval, Timeout                                              time.Duration
	Once, DryRun, AllowHTTP                                        bool
}

func configFlags(args []string, name string, sampling bool, output io.Writer) (config, error) {
	var cfg config
	flags := flag.NewFlagSet(name, flag.ContinueOnError)
	flags.SetOutput(output)
	defaultPath := ""
	if dir, err := os.UserConfigDir(); err == nil {
		defaultPath = filepath.Join(dir, "curio", "collector.yaml")
	}
	flags.StringVar(&cfg.ConfigFile, "config", defaultPath, "configuration file")
	if sampling {
		flags.BoolVar(&cfg.Once, "once", false, "collect and post once")
		flags.BoolVar(&cfg.DryRun, "dry-run", false, "print one snapshot without posting")
	}
	flags.Usage = func() {
		fmt.Fprintf(output, "Usage: %s [options]\n", name)
		flags.PrintDefaults()
		if sampling {
			fmt.Fprintln(output, "Commands: init, check, export, run")
			if runtime.GOOS == "windows" {
				fmt.Fprintln(output, "Windows: service install [--config PATH], service uninstall, service config")
			}
		}
	}
	if err := flags.Parse(args); err != nil {
		return cfg, err
	}
	if flags.NArg() > 0 {
		return cfg, errors.New("unexpected positional arguments")
	}
	if cfg.ConfigFile == "" {
		return cfg, errors.New("set --config to a configuration file")
	}
	if cfg.Once && cfg.DryRun {
		return cfg, errors.New("choose --once or --dry-run")
	}
	var err error
	cfg.ConfigFile, err = filepath.Abs(cfg.ConfigFile)
	return cfg, err
}

func parseConfig(args []string, output io.Writer) (config, error) {
	cfg, err := configFlags(args, "curio-collector", true, output)
	if err != nil {
		return cfg, err
	}
	return loadLocalConfig(cfg)
}

var hostIDPattern = regexp.MustCompile(`^[A-Za-z0-9._-]{1,48}$`)
var tokenPattern = regexp.MustCompile(`^[!-~]{16,128}$`)

func (cfg *config) readIdentity() error {
	if cfg.HostID == "" {
		data, err := os.ReadFile(cfg.IdentityFile)
		if errors.Is(err, os.ErrNotExist) {
			return nil
		}
		if err != nil {
			return fmt.Errorf("identity_file: %w", err)
		}
		cfg.HostID = strings.TrimSpace(string(data))
	}
	if !hostIDPattern.MatchString(cfg.HostID) {
		return errors.New("host ID must be 1–48 ASCII letters, digits, '.', '_' or '-'")
	}
	return nil
}

func (cfg *config) prepareIdentity() error {
	if err := cfg.readIdentity(); err != nil || cfg.HostID != "" {
		return err
	}
	var random [16]byte
	if _, err := rand.Read(random[:]); err != nil {
		return err
	}
	cfg.HostID = "host-" + hex.EncodeToString(random[:])
	return createFile(cfg.IdentityFile, []byte(cfg.HostID+"\n"))
}

func (cfg *config) loadToken() error {
	if cfg.TokenFile == "" {
		return errors.New("token_file is required")
	}
	data, err := os.ReadFile(cfg.TokenFile)
	if err != nil {
		return fmt.Errorf("token_file: %w", err)
	}
	cfg.Token = strings.TrimSpace(string(data))
	if !tokenPattern.MatchString(cfg.Token) {
		return errors.New("token_file must contain 16–128 ASCII characters from '!' through '~'")
	}
	return nil
}
