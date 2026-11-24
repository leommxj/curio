package main

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"time"

	"gopkg.in/yaml.v3"
)

type localConfig struct {
	Version      int                     `yaml:"version,omitempty"`
	Endpoint     string                  `yaml:"endpoint,omitempty"`
	CA           string                  `yaml:"ca,omitempty"`
	TokenFile    string                  `yaml:"token_file,omitempty"`
	IdentityFile string                  `yaml:"identity_file,omitempty"`
	HostID       string                  `yaml:"host_id,omitempty"`
	HostName     string                  `yaml:"host_name,omitempty"`
	Interval     string                  `yaml:"publish_interval,omitempty"`
	Timeout      string                  `yaml:"timeout,omitempty"`
	AllowHTTP    bool                    `yaml:"allow_http_loopback,omitempty"`
	Sources      map[string]sourceConfig `yaml:"sources"`
}
type sourceConfig struct {
	Module                    string            `yaml:"module,omitempty"`
	Interval                  string            `yaml:"interval,omitempty"`
	MaxAge                    string            `yaml:"max_age,omitempty"`
	Path                      string            `yaml:"path,omitempty"`
	Interface                 string            `yaml:"interface,omitempty"`
	Timeout                   string            `yaml:"timeout,omitempty"`
	Fields                    []string          `yaml:"fields,omitempty"`
	Socket                    string            `yaml:"socket,omitempty"`
	Session                   string            `yaml:"session,omitempty"`
	Binary                    string            `yaml:"binary,omitempty"`
	CLI                       bool              `yaml:"cli,omitempty"`
	Project                   string            `yaml:"project,omitempty"`
	AllowTitles               bool              `yaml:"allow_titles,omitempty"`
	AllowProjects             []string          `yaml:"allow_projects,omitempty"`
	AllowAgents               []string          `yaml:"allow_agents,omitempty"`
	AllowPanes                []string          `yaml:"allow_panes,omitempty"`
	Aliases                   map[string]string `yaml:"project_aliases,omitempty"`
	interval, timeout, maxAge time.Duration
}

var moduleFields = map[string]map[string]string{
	"disk":    {"usage_pct": "percent", "used_bytes": "bytes", "free_bytes": "bytes", "total_bytes": "bytes"},
	"network": {"rx_bytes_per_second": "bytes_per_second", "tx_bytes_per_second": "bytes_per_second"},
	"cpu":     {"usage_pct": "percent", "logical_count": "count"},
	"memory":  {"usage_pct": "percent", "used_bytes": "bytes", "total_bytes": "bytes", "available_bytes": "bytes"},
}

func duration(value string, fallback time.Duration) (time.Duration, error) {
	if value == "" {
		return fallback, nil
	}
	d, err := time.ParseDuration(value)
	if err != nil || d <= 0 {
		return 0, fmt.Errorf("invalid positive duration %q", value)
	}
	return d, nil
}
func defaultMaxAge(interval time.Duration) time.Duration {
	if interval > 7*24*time.Hour/3 {
		return 7*24*time.Hour + time.Millisecond
	}
	age := max(10*time.Second, 3*interval)
	return ((age + time.Millisecond - 1) / time.Millisecond) * time.Millisecond
}

func loadLocalConfig(cfg config) (config, error) {
	f, err := os.Open(cfg.ConfigFile)
	if errors.Is(err, os.ErrNotExist) {
		return cfg, fmt.Errorf("config not found: %s; run curio-collector init", cfg.ConfigFile)
	}
	if err != nil {
		return cfg, err
	}
	defer f.Close()
	local, err := readLocalConfig(f)
	if err != nil {
		return cfg, err
	}
	if len(local.Sources) > 8 {
		return cfg, errors.New("sources exceed capacity 8")
	}
	cfg.Local = &local
	base := filepath.Dir(cfg.ConfigFile)
	resolve := func(path string) string {
		if path == "" || filepath.IsAbs(path) {
			return path
		}
		return filepath.Join(base, path)
	}
	cfg.Endpoint, cfg.HostID = local.Endpoint, local.HostID
	cfg.CA, cfg.TokenFile = resolve(local.CA), resolve(local.TokenFile)
	if cfg.HostID != "" && !hostIDPattern.MatchString(cfg.HostID) {
		return cfg, errors.New("invalid host_id")
	}
	cfg.AllowHTTP = local.AllowHTTP
	dir, _ := os.UserConfigDir()
	cfg.IdentityFile = filepath.Join(base, "host-id")
	if local.IdentityFile != "" {
		cfg.IdentityFile = resolve(local.IdentityFile)
	}
	hostname, _ := os.Hostname()
	if hostname == "" {
		hostname = "host"
	}
	cfg.HostName = shorten(hostname, 48)
	if local.HostName != "" {
		cfg.HostName = local.HostName
	}
	if !validWireString(cfg.HostName, 1, 48) {
		return cfg, errors.New("host_name must contain 1–48 UTF-8 bytes without NUL")
	}
	if cfg.Interval, err = duration(local.Interval, 2*time.Second); err != nil {
		return cfg, err
	}
	if cfg.Timeout, err = duration(local.Timeout, 5*time.Second); err != nil {
		return cfg, err
	}
	count := 0
	for id, s := range local.Sources {
		if !hostIDPattern.MatchString(id) {
			return cfg, fmt.Errorf("invalid source ID %q", id)
		}
		if s.Module != "herdr" && moduleFields[s.Module] == nil {
			return cfg, fmt.Errorf("source %s: unknown module %q", id, s.Module)
		}
		if s.interval, err = duration(s.Interval, 2*time.Second); err != nil {
			return cfg, fmt.Errorf("source %s interval: %w", id, err)
		}
		if s.interval > 7*24*time.Hour {
			return cfg, fmt.Errorf("source %s: interval must not exceed 7 days", id)
		}
		fallback := defaultMaxAge(s.interval)
		if fallback > 7*24*time.Hour && s.MaxAge == "" {
			return cfg, fmt.Errorf("source %s: default max_age exceeds 7 days; set max_age explicitly", id)
		}
		if s.maxAge, err = duration(s.MaxAge, fallback); err != nil {
			return cfg, fmt.Errorf("source %s max_age: %w", id, err)
		}
		if s.maxAge%time.Millisecond != 0 || s.maxAge > 7*24*time.Hour || s.maxAge < s.interval {
			return cfg, fmt.Errorf("source %s: max_age must be positive integer milliseconds, cover interval, and not exceed 7 days", id)
		}
		if s.Module == "disk" {
			if s.Path == "" {
				return cfg, fmt.Errorf("source %s: disk requires path", id)
			}
			s.Path = resolve(s.Path)
		}
		if s.Module == "network" && strings.TrimSpace(s.Interface) == "" {
			return cfg, fmt.Errorf("source %s: network requires interface", id)
		}
		if s.timeout, err = duration(s.Timeout, cfg.Timeout); err != nil {
			return cfg, fmt.Errorf("source %s timeout: %w", id, err)
		}
		if s.Module == "herdr" {
			if s.Project == "" {
				s.Project = "basename"
			}
			if s.Project != "basename" && s.Project != "none" {
				return cfg, errors.New("project must be basename or none")
			}
			if s.Binary == "" {
				s.Binary = "herdr"
			} else if strings.ContainsAny(s.Binary, `/\`) {
				s.Binary = resolve(s.Binary)
			}
			s.Socket = resolve(s.Socket)
			if s.Session != "" || s.CLI {
				s.Socket = ""
			} else if s.Socket == "" && runtime.GOOS != "windows" {
				s.Socket = filepath.Join(dir, "herdr", "herdr.sock")
			}
		} else {
			if s.Fields == nil {
				for key := range moduleFields[s.Module] {
					s.Fields = append(s.Fields, key)
				}
			}
			sort.Strings(s.Fields)
			seen := map[string]bool{}
			for _, key := range s.Fields {
				if moduleFields[s.Module][key] == "" || seen[key] {
					return cfg, fmt.Errorf("source %s: invalid or duplicate field %q", id, key)
				}
				seen[key] = true
				count++
			}
		}
		local.Sources[id] = s
	}
	if count > 32 {
		return cfg, errors.New("metrics exceed capacity 32")
	}
	return cfg, nil
}

func readLocalConfig(r io.Reader) (localConfig, error) {
	var local localConfig
	data, err := io.ReadAll(io.LimitReader(r, 65537))
	if err != nil {
		return local, err
	}
	if len(data) > 65536 {
		return local, errors.New("config exceeds 64 KiB")
	}
	decoder := yaml.NewDecoder(bytes.NewReader(data))
	decoder.KnownFields(true)
	local.Version = 1
	if err := decoder.Decode(&local); err != nil {
		return local, fmt.Errorf("config: %w", err)
	}
	if err := decoder.Decode(new(any)); err != io.EOF {
		return local, errors.New("config must contain one YAML mapping")
	}
	if local.Version != 1 || local.Sources == nil {
		return local, errors.New("config requires version 1 and sources mapping")
	}
	return local, nil
}

func exportConfig(path string, output io.Writer) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	local, err := readLocalConfig(f)
	if err != nil {
		return err
	}
	local.Version = 0
	if local.IdentityFile == "host-id" {
		local.IdentityFile = ""
	}
	if local.Interval == "2s" {
		local.Interval = ""
	}
	if local.Timeout == "5s" {
		local.Timeout = ""
	}
	for id, source := range local.Sources {
		if fields := moduleFields[source.Module]; fields != nil && len(source.Fields) == len(fields) {
			all := true
			seen := map[string]bool{}
			for _, field := range source.Fields {
				if fields[field] == "" || seen[field] {
					all = false
				}
				seen[field] = true
			}
			if all {
				source.Fields = nil
			}
		}
		interval, err := duration(source.Interval, 2*time.Second)
		if err != nil {
			return err
		}
		if fallback := defaultMaxAge(interval); fallback <= 7*24*time.Hour {
			if age, err := duration(source.MaxAge, fallback); err == nil && age == fallback {
				source.MaxAge = ""
			}
		}
		if source.Interval == "2s" {
			source.Interval = ""
		}
		timeout := local.Timeout
		if timeout == "" {
			timeout = "5s"
		}
		if source.Timeout == timeout {
			source.Timeout = ""
		}
		if source.Module == "herdr" {
			if source.Project == "basename" {
				source.Project = ""
			}
			if source.Binary == "herdr" {
				source.Binary = ""
			}
		}
		local.Sources[id] = source
	}
	encoder := yaml.NewEncoder(output)
	encoder.SetIndent(2)
	defer encoder.Close()
	return encoder.Encode(local)
}

func (s sourceConfig) MarshalYAML() (any, error) {
	type plain sourceConfig
	var node yaml.Node
	if err := node.Encode(plain(s)); err != nil {
		return nil, err
	}
	if s.Fields != nil && len(s.Fields) == 0 {
		node.Content = append(node.Content, &yaml.Node{Kind: yaml.ScalarNode, Value: "fields"}, &yaml.Node{Kind: yaml.SequenceNode, Tag: "!!seq", Style: yaml.FlowStyle})
	}
	return &node, nil
}

func (s *sourceConfig) UnmarshalYAML(node *yaml.Node) error {
	type plain sourceConfig
	var decoded plain
	if err := node.Decode(&decoded); err != nil {
		return err
	}
	module := decoded.Module
	common := map[string]bool{"module": true, "interval": true, "timeout": true, "max_age": true}
	moduleOptions := map[string]string{"fields": "metrics", "path": "disk", "interface": "network", "socket": "herdr", "session": "herdr", "binary": "herdr", "cli": "herdr", "project": "herdr", "allow_titles": "herdr", "allow_projects": "herdr", "allow_agents": "herdr", "allow_panes": "herdr", "project_aliases": "herdr"}
	for i := 0; i+1 < len(node.Content); i += 2 {
		key := node.Content[i].Value
		if common[key] {
			continue
		}
		owner, ok := moduleOptions[key]
		if !ok {
			return fmt.Errorf("unknown source option %q", key)
		}
		if owner == "metrics" && moduleFields[module] != nil {
			continue
		}
		if owner != module {
			return fmt.Errorf("option %s is not valid for module %s", key, module)
		}
	}
	*s = sourceConfig(decoded)
	return nil
}
