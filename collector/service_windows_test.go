package main

import (
	"bytes"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
	"time"

	"golang.org/x/sys/windows"
	"golang.org/x/sys/windows/svc"
)

func TestWindowsServiceLifecycle(t *testing.T) {
	for _, stop := range []svc.Cmd{svc.Stop, svc.Shutdown} {
		t.Run(map[svc.Cmd]string{svc.Stop: "stop", svc.Shutdown: "shutdown"}[stop], func(t *testing.T) {
			received := make(chan struct{}, 1)
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				io.Copy(io.Discard, r.Body)
				received <- struct{}{}
				<-r.Context().Done()
			}))
			t.Cleanup(server.Close)
			path := testLocalConfig(t, "endpoint: "+server.URL+"\nallow_http_loopback: true\ntoken_file: token\nsources: {}\n")
			if err := os.WriteFile(filepath.Join(filepath.Dir(path), "token"), []byte("test-service-token"), 0600); err != nil {
				t.Fatal(err)
			}
			requests := make(chan svc.ChangeRequest, 4)
			changes := make(chan svc.Status, 8)
			finished := make(chan bool, 1)
			var log bytes.Buffer
			go func() {
				failed, code := (&collectorService{[]string{"--config", path}, &log}).Execute(nil, requests, changes)
				finished <- failed || code != 0
			}()
			t.Cleanup(func() { requests <- svc.ChangeRequest{Cmd: svc.Stop} })
			expect := func(state svc.State) svc.Status {
				t.Helper()
				select {
				case status := <-changes:
					if status.State != state {
						t.Fatalf("state %d, want %d", status.State, state)
					}
					return status
				case <-time.After(3 * time.Second):
					t.Fatal("no service status")
					return svc.Status{}
				}
			}
			expect(svc.StartPending)
			status := expect(svc.Running)
			if status.Accepts != svc.AcceptStop|svc.AcceptShutdown {
				t.Fatal("service does not accept stop and shutdown")
			}
			select {
			case <-received:
			case <-time.After(3 * time.Second):
				t.Fatal("service did not use the Collector reporting loop")
			}
			requests <- svc.ChangeRequest{Cmd: svc.Interrogate}
			expect(svc.Running)
			requests <- svc.ChangeRequest{Cmd: stop}
			expect(svc.StopPending)
			select {
			case failed := <-finished:
				if failed || log.Len() != 0 {
					t.Fatal("service shutdown failed", log.String())
				}
			case <-time.After(3 * time.Second):
				t.Fatal("service did not cancel its pending HTTP request")
			}
		})
	}
}

func TestWindowsServiceRejectsBadConfiguration(t *testing.T) {
	path := testLocalConfig(t, "endpoint: https://localhost\ntoken_file: missing\nsources: {}\n")
	for _, args := range [][]string{{"--config", path}, {"--config", path, "--once"}, {"--config", path, "--dry-run"}} {
		changes := make(chan svc.Status, 8)
		var log bytes.Buffer
		failed, code := (&collectorService{args, &log}).Execute(nil, nil, changes)
		if !failed || code == 0 || log.Len() == 0 || len(changes) != 1 || (<-changes).State != svc.StartPending {
			t.Fatal("service accepted invalid configuration", args)
		}
		if _, err := os.Stat(filepath.Join(filepath.Dir(path), "host-id")); !os.IsNotExist(err) {
			t.Fatal("failed validation created identity", err)
		}
	}
	for _, args := range [][]string{nil, {"start"}, {"uninstall", "extra"}, {"config", "extra"}} {
		if err := manageService(args); err == nil {
			t.Fatal("invalid service command accepted", args)
		}
	}
}

func TestServicePaths(t *testing.T) {
	for _, path := range []string{`C:\Users\Alice\collector.yaml`, `D:\状态 files\O'Brien & $data\collector.yaml`, `\\server\share\config.yaml`} {
		executable := `C:\Program Files\Curio\curio-collector.exe`
		for _, flags := range [][]string{{"--config", path}, {"--config=" + path}} {
			command := windows.ComposeCommandLine(append([]string{executable}, flags...))
			gotExe, gotPath, err := servicePaths(command)
			if err != nil || gotExe != executable || gotPath != path {
				t.Fatalf("service path changed: %q %q %v", gotExe, gotPath, err)
			}
		}
	}
	for _, args := range [][]string{
		{}, {`C:\curio.exe`}, {`C:\curio.exe`, "--config", "relative.yaml"},
		{"relative.exe", "--config", `C:\config.yaml`}, {`C:\curio.exe`, "--help"},
		{`C:\curio.exe`, "--config", `C:\config.yaml`, "--once"},
		{`C:\curio.exe`, "--config", `C:\config.yaml`, "extra"},
	} {
		if _, _, err := servicePaths(windows.ComposeCommandLine(args)); err == nil {
			t.Fatal("accepted invalid registered command", args)
		}
	}
}

func TestServiceBinaryReplacement(t *testing.T) {
	dir := t.TempDir()
	source, target := filepath.Join(dir, "new.exe"), filepath.Join(dir, "installed", "curio.exe")
	if err := os.WriteFile(source, []byte("first"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := replaceServiceBinary(source, target); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(source, []byte("second"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := replaceServiceBinary(source, target); err != nil {
		t.Fatal(err)
	}
	if err := replaceServiceBinary(target, target); err != nil {
		t.Fatal(err)
	}
	if err := replaceServiceBinary(filepath.Join(dir, "missing.exe"), target); err == nil {
		t.Fatal("accepted a missing upgrade")
	}
	data, err := os.ReadFile(target)
	if err != nil || string(data) != "second" {
		t.Fatal("failed upgrade changed the installed binary", err)
	}
	files, err := os.ReadDir(filepath.Dir(target))
	if err != nil || len(files) != 1 {
		t.Fatal("temporary upgrade files left behind", err)
	}
}

func TestServiceAccountSID(t *testing.T) {
	for account, want := range map[string]string{"LocalSystem": "S-1-5-18", `NT AUTHORITY\LocalService`: "S-1-5-19", `nt authority\networkservice`: "S-1-5-20"} {
		sid, err := serviceAccountSID(account)
		if err != nil || sid.String() != want {
			t.Fatalf("%s resolved to %v: %v", account, sid, err)
		}
	}
}
