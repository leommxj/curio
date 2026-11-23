package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"
)

func TestDefaultConfigInitAndPersistentIdentity(t *testing.T) {
	if runtime.GOOS != "linux" {
		t.Skip("Linux config directory")
	}
	base := t.TempDir()
	t.Setenv("XDG_CONFIG_HOME", base)
	p := filepath.Join(base, "curio", "collector.yaml")
	var out bytes.Buffer
	if err := run(context.Background(), []string{"init"}, &out, io.Discard); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(p)
	if err != nil || !bytes.Equal(data, exampleConfig) {
		t.Fatalf("init differs from maintained example: %v", err)
	}
	info, _ := os.Stat(p)
	if info.Mode().Perm() != 0600 {
		t.Fatal("config permissions", info.Mode())
	}
	cfg, err := parseConfig(nil, io.Discard)
	if err != nil || cfg.ConfigFile != p || cfg.IdentityFile != filepath.Join(base, "curio", "host-id") {
		t.Fatalf("default config: %+v %v", cfg, err)
	}
	custom := []byte(`{"version":1,"sources":{}}`)
	if err := os.WriteFile(p, custom, 0600); err != nil {
		t.Fatal(err)
	}
	if err := run(context.Background(), []string{"init"}, io.Discard, io.Discard); !errors.Is(err, os.ErrExist) {
		t.Fatal("init must reject an existing config", err)
	}
	data, _ = os.ReadFile(p)
	if !bytes.Equal(data, custom) {
		t.Fatal("init overwrote user config")
	}
	var identity string
	for i := 0; i < 2; i++ {
		out.Reset()
		if err := run(context.Background(), []string{"--dry-run"}, &out, io.Discard); err != nil {
			t.Fatal(err)
		}
		var snapshot snapshotV2
		if err := json.Unmarshal(out.Bytes(), &snapshot); err != nil || snapshot.Version != 2 || snapshot.HostID == "" {
			t.Fatal("invalid default snapshot", out.String(), err)
		}
		if i == 0 {
			identity = snapshot.HostID
		} else if snapshot.HostID != identity {
			t.Fatal("restart changed host identity")
		}
	}
}

func TestCheckResolvesConfigPathsWithoutNetworkOrIdentityCreation(t *testing.T) {
	requests := make(chan struct{}, 1)
	server := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests <- struct{}{}
	}))
	defer server.Close()
	p := testLocalConfig(t, `{"version":1,"endpoint":"`+server.URL+`","ca":"cert.pem","token_file":"token.txt","identity_file":"identity/host-id","sources":{"herdr":{"module":"herdr","socket":"herdr.sock","binary":"./bin/herdr"}}}`)
	base := filepath.Dir(p)
	cert, _ := os.ReadFile(serverCA(t, server))
	for name, data := range map[string][]byte{"cert.pem": cert, "token.txt": []byte("local-test-secret")} {
		if err := os.WriteFile(filepath.Join(base, name), data, 0600); err != nil {
			t.Fatal(err)
		}
	}
	cfg, err := parseConfig([]string{"--config", p}, io.Discard)
	if err != nil {
		t.Fatal(err)
	}
	s := cfg.Local.Sources["herdr"]
	if cfg.CA != filepath.Join(base, "cert.pem") || cfg.TokenFile != filepath.Join(base, "token.txt") || s.Socket != filepath.Join(base, "herdr.sock") || s.Binary != filepath.Join(base, "bin", "herdr") {
		t.Fatalf("paths depend on working directory: %+v %+v", cfg, s)
	}
	if err := run(context.Background(), []string{"check", "--config", p}, io.Discard, io.Discard); err != nil {
		t.Fatal(err)
	}
	select {
	case <-requests:
		t.Fatal("check contacted the Hub")
	default:
	}
	if _, err := os.Stat(cfg.IdentityFile); !errors.Is(err, os.ErrNotExist) {
		t.Fatal("check created an identity", err)
	}
	for _, name := range []string{"cert.pem", "token.txt", "identity/host-id"} {
		t.Run(name, func(t *testing.T) {
			file := filepath.Join(base, name)
			previous, readErr := os.ReadFile(file)
			t.Cleanup(func() {
				if readErr == nil {
					os.WriteFile(file, previous, 0600)
				} else {
					os.Remove(file)
				}
			})
			if err := os.MkdirAll(filepath.Dir(file), 0700); err != nil {
				t.Fatal(err)
			}
			if err := os.WriteFile(file, []byte("invalid value"), 0600); err != nil {
				t.Fatal(err)
			}
			if err := checkConfig(cfg); err == nil {
				t.Fatal("check accepted invalid", name)
			}
		})
	}
}

func TestConfigCommandsRejectLegacyAndConflictingOptions(t *testing.T) {
	p := testLocalConfig(t, `{"version":1,"sources":{}}`)
	for _, args := range [][]string{
		{"--config", p, "--endpoint", "https://old"},
		{"--config", p, "--once", "--dry-run"},
		{"check", "--config", p, "--once"},
		{"init", "--config", p, "--dry-run"},
		{"service", "install"},
		{"service", "uninstall"},
		{"unknown"},
	} {
		if err := run(context.Background(), args, io.Discard, io.Discard); err == nil {
			t.Error("accepted", args)
		}
	}
	t.Setenv("CURIO_TOKEN", "ignored-secret")
	cfg, err := parseConfig([]string{"--config", p}, io.Discard)
	if err != nil {
		t.Fatal(err)
	}
	if err := cfg.loadToken(); err == nil {
		t.Fatal("token loaded from legacy environment")
	}
}

func TestYAMLStrictnessAndDefaults(t *testing.T) {
	for _, input := range []string{
		"sources: {}\nunknown: true\n",
		"sources: {}\nsources: {}\n",
		"sources: {}\n---\nsources: {}\n",
		"sources: {cpu: {module: cpu, unknown: true}}\n",
		"sources: {}\n#" + strings.Repeat("x", 65536),
	} {
		if _, err := readLocalConfig(strings.NewReader(input)); err == nil {
			t.Errorf("accepted invalid configuration: %.100s", input)
		}
	}
	p := testLocalConfig(t, "token_file: missing-secret\nsources:\n  cpu:\n    module: cpu\n    fields: []\n  herdr:\n    module: herdr\n    allow_agents: [agent-1]\n    project_aliases: {project: display}\n")
	cfg, err := parseConfig([]string{"--config", p}, io.Discard)
	if err != nil {
		t.Fatal(err)
	}
	if cfg.Interval != 2*time.Second || cfg.Timeout != 5*time.Second || cfg.Local.Sources["herdr"].Project != "basename" {
		t.Fatal("missing defaults", cfg)
	}
	var output bytes.Buffer
	if err := run(context.Background(), []string{"export", "--config", p}, &output, io.Discard); err != nil {
		t.Fatal(err)
	}
	local, err := readLocalConfig(bytes.NewReader(output.Bytes()))
	if err != nil {
		t.Fatal(err)
	}
	if local.Sources["cpu"].Fields == nil || len(local.Sources["cpu"].Fields) != 0 || local.Sources["herdr"].Aliases["project"] != "display" || local.TokenFile != "missing-secret" {
		t.Fatal("export changed configuration", output.String())
	}
}

func TestTokenMatchesHubContract(t *testing.T) {
	path := filepath.Join(t.TempDir(), "token")
	cfg := config{TokenFile: path}
	for _, token := range []string{
		"", strings.Repeat("a", 15), strings.Repeat("a", 129),
		"secret token-1234", "secret\x00token-1234", "secret\vtoken-1234",
		"secret\x7ftoken-1234", "secretétoken-1234", "secret\xfftoken-1234",
	} {
		if err := os.WriteFile(path, []byte(token), 0600); err != nil {
			t.Fatal(err)
		}
		if err := cfg.loadToken(); err == nil {
			t.Errorf("accepted token %q", token)
		}
	}
	for _, token := range []string{strings.Repeat("!", 16), strings.Repeat("~", 128)} {
		if err := os.WriteFile(path, []byte(token+"\n"), 0600); err != nil {
			t.Fatal(err)
		}
		if err := cfg.loadToken(); err != nil || cfg.Token != token {
			t.Fatalf("valid token of length %d with trailing newline: %v", len(token), err)
		}
	}
}
