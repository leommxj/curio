package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"testing"
	"time"
)

func mockSocket(t *testing.T, payload string) string {
	t.Helper()
	if runtime.GOOS == "windows" {
		t.Skip("Unix socket test")
	}
	socket := filepath.Join(t.TempDir(), "herdr.sock")
	listener, err := net.Listen("unix", socket)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { listener.Close() })
	go func() {
		conn, err := listener.Accept()
		if err != nil {
			return
		}
		defer conn.Close()
		request, err := bufio.NewReader(conn).ReadBytes('\n')
		if err != nil {
			return
		}
		var call struct {
			Method string `json:"method"`
		}
		if json.Unmarshal(request, &call) != nil || call.Method != "agent.list" {
			t.Error("collector made a non-read-only call")
		}
		io.WriteString(conn, payload+"\n")
	}()
	return socket
}

func TestDryRunSourceAndNoToken(t *testing.T) {
	socket := mockSocket(t, `{"result":{"type":"agent_list","agents":[]}}`)
	var stdout, stderr bytes.Buffer
	p := testLocalConfig(t, fmt.Sprintf(`{"version":1,"host_id":"test","sources":{"herdr":{"module":"herdr","socket":%q}}}`, socket))
	err := run(context.Background(), []string{"--config", p, "--dry-run"}, &stdout, &stderr)
	if err != nil {
		t.Fatal(err)
	}
	var snapshot snapshotV2
	if err := json.Unmarshal(stdout.Bytes(), &snapshot); err != nil || snapshot.Sources["herdr"].Status != "ok" || snapshot.Sources["herdr"].Agents == nil || len(*snapshot.Sources["herdr"].Agents) != 0 {
		t.Fatal(stdout.String())
	}
}

func TestRunCommand(t *testing.T) {
	p := testLocalConfig(t, "host_id: test\nsources: {}\n")
	var output bytes.Buffer
	if err := run(context.Background(), []string{"run", "--config", p, "--dry-run"}, &output, io.Discard); err != nil {
		t.Fatal(err)
	}
	var snapshot snapshotV2
	if err := json.Unmarshal(output.Bytes(), &snapshot); err != nil || snapshot.HostID != "test" || snapshot.Version != 2 {
		t.Fatal("run did not collect", output.String(), err)
	}
	for _, args := range [][]string{{"run", "init", "--config", p}, {"run", "--config", p, "unexpected"}} {
		if err := run(context.Background(), args, io.Discard, io.Discard); err == nil {
			t.Fatal("accepted unexpected arguments", args)
		}
	}
}

func TestUnavailableSnapshotPostedAndFailsOnce(t *testing.T) {
	var received snapshotV2
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if err := json.NewDecoder(r.Body).Decode(&received); err != nil {
			t.Error(err)
		}
		io.WriteString(w, `{"ok":true}`)
	}))
	defer server.Close()
	var stdout, stderr bytes.Buffer
	p := testLocalConfig(t, fmt.Sprintf(`{"version":1,"host_id":"test","endpoint":%q,"token_file":"token.txt","allow_http_loopback":true,"sources":{"herdr":{"module":"herdr","socket":"missing.sock"}}}`, server.URL))
	if err := os.WriteFile(filepath.Join(filepath.Dir(p), "token.txt"), []byte("test-collector-secret"), 0600); err != nil {
		t.Fatal(err)
	}
	err := run(context.Background(), []string{"--config", p, "--once"}, &stdout, &stderr)
	if err == nil {
		t.Fatal("unavailable --once should fail")
	}
	source := received.Sources["herdr"]
	if source.Status != "error" || source.Agents == nil || len(*source.Agents) != 0 || received.HostID != "test" {
		t.Fatalf("bad unavailable snapshot: %+v", received)
	}
	if strings.Contains(stderr.String(), "test-collector-secret") {
		t.Fatal("token leaked")
	}
}

func TestIdentityAndConfigValidation(t *testing.T) {
	file := filepath.Join(t.TempDir(), "config", "host-id")
	first := config{IdentityFile: file}
	if err := first.prepareIdentity(); err != nil {
		t.Fatal(err)
	}
	second := config{IdentityFile: file}
	if err := second.prepareIdentity(); err != nil || first.HostID != second.HostID {
		t.Fatalf("identity not persistent: %v", err)
	}
	if runtime.GOOS != "windows" {
		info, _ := os.Stat(file)
		if info.Mode().Perm() != 0600 {
			t.Fatal("identity permissions")
		}
	}
	for _, fields := range []string{`"host_name":"` + strings.Repeat("界", 17) + `"`, `"host_name":"host\u0000suffix"`, `"publish_interval":"0s"`, `"timeout":"0s"`, `"host_id":"not valid"`} {
		p := testLocalConfig(t, `{"version":1,"sources":{},`+fields+`}`)
		if _, err := parseConfig([]string{"--config", p}, io.Discard); err == nil {
			t.Errorf("accepted %s", fields)
		}
	}
	bad := config{HostID: "not valid"}
	if err := bad.prepareIdentity(); err == nil {
		t.Fatal("invalid host ID")
	}
	p := testLocalConfig(t, `{"version":1,"sources":{"herdr":{"module":"herdr","session":"work","socket":"ignored"}}}`)
	cfg, err := parseConfig([]string{"--config", p}, io.Discard)
	if err != nil || cfg.Local.Sources["herdr"].Socket != "" || cfg.Local.Sources["herdr"].Session != "work" {
		t.Fatalf("session CLI selection: %+v %v", cfg, err)
	}
}

func TestSourceTimeout(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("Unix socket test")
	}
	socket := filepath.Join(t.TempDir(), "herdr.sock")
	listener, err := net.Listen("unix", socket)
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	go func() {
		conn, err := listener.Accept()
		if err == nil {
			defer conn.Close()
			io.Copy(io.Discard, conn)
		}
	}()
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Millisecond)
	defer cancel()
	if _, err := (herdrSource{Socket: socket}).read(ctx); err == nil {
		t.Fatal("missing source timeout")
	}
}

func TestCLITimeoutClosesInheritedOutput(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("shell subprocess test")
	}
	dir := t.TempDir()
	binary := filepath.Join(dir, "herdr")
	pidFile := filepath.Join(dir, "child-pid")
	if err := os.WriteFile(binary, []byte("#!/bin/sh\nsleep 10 &\necho $! > \""+pidFile+"\"\nwait\n"), 0700); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		data, _ := os.ReadFile(pidFile)
		pid, err := strconv.Atoi(strings.TrimSpace(string(data)))
		if err == nil {
			process, _ := os.FindProcess(pid)
			process.Kill()
		}
	})
	ctx, cancel := context.WithTimeout(context.Background(), 100*time.Millisecond)
	defer cancel()
	start := time.Now()
	if _, err := (herdrSource{Binary: binary}).read(ctx); err == nil {
		t.Fatal("missing CLI timeout")
	}
	if time.Since(start) > time.Second {
		t.Fatal("CLI reader waited for inherited output after cancellation")
	}
}
