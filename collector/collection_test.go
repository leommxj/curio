package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"github.com/shirou/gopsutil/v4/cpu"
	"math"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"
)

func testLocalConfig(t *testing.T, body string) string {
	t.Helper()
	p := filepath.Join(t.TempDir(), "config.yaml")
	if err := os.WriteFile(p, []byte(body), 0600); err != nil {
		t.Fatal(err)
	}
	return p
}
func TestLocalConfigIsolation(t *testing.T) {
	t.Setenv("CURIO_HOST_ID", "legacy")
	t.Setenv("CURIO_ENDPOINT", "https://legacy")
	p := testLocalConfig(t, `{"version":1,"host_id":"local","sources":{}}`)
	cfg, err := parseConfig([]string{"--config", p}, &bytes.Buffer{})
	if err != nil {
		t.Fatal(err)
	}
	if cfg.HostID != "local" || cfg.Endpoint != "" {
		t.Fatal("legacy environment leaked")
	}
	if _, err = parseConfig([]string{"--config", p, "--interval", "1s"}, &bytes.Buffer{}); err == nil {
		t.Fatal("accepted mixed policy")
	}
}
func TestLocalFieldsValidation(t *testing.T) {
	for _, body := range []string{
		`{"version":1,"sources":{"system.cpu":{"module":"cpu","fields":["private"]}}}`,
		`{"version":1,"sources":{"bad key":{"module":"cpu"}}}`,
		`{"version":1,"sources":{}} {}`,
	} {
		p := testLocalConfig(t, body)
		if _, err := parseConfig([]string{"--config", p}, &bytes.Buffer{}); err == nil {
			t.Fatalf("accepted %s", body)
		}
	}
}
func TestV2EmptySnapshot(t *testing.T) {
	p := testLocalConfig(t, `{"version":1,"host_id":"empty","sources":{}}`)
	var out bytes.Buffer
	if err := run(context.Background(), []string{"--config", p, "--dry-run"}, &out, &bytes.Buffer{}); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out.String(), `"sources":{}`) || !strings.Contains(out.String(), `"v":2`) {
		t.Fatal(out.String())
	}
}
func TestV2ProjectionAgeAndFailure(t *testing.T) {
	s := sourceConfig{Module: "memory", Fields: []string{"used_bytes"}}
	out := failure(s)
	out.sampled = time.Now().Add(-time.Second)
	c := collection{latest: map[string]sourceSnapshot{"system.memory": out}}
	data, err := c.encode(config{HostID: "test", HostName: "Test"})
	if err != nil {
		t.Fatal(err)
	}
	var wire snapshotV2
	if err = json.Unmarshal(data, &wire); err != nil {
		t.Fatal(err)
	}
	got := wire.Sources["system.memory"]
	if got.Status != "error" || got.Age < 1000 || len(got.Metrics) != 1 || got.Metrics["used_bytes"].Value != nil {
		t.Fatal(string(data))
	}
}
func TestRealCPUNextSample(t *testing.T) {
	m := localModule{config: sourceConfig{Module: "cpu", Fields: []string{"usage_pct"}}}
	first, err := m.sample(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if first.Status != "warming_up" {
		t.Fatal(first.Status)
	}
	var next sourceSnapshot
	for deadline := time.Now().Add(2 * time.Second); time.Now().Before(deadline); {
		time.Sleep(50 * time.Millisecond)
		next, err = m.sample(context.Background())
		if err != nil {
			t.Fatal(err)
		}
		if next.Status == "ok" {
			break
		}
	}
	v := next.Metrics["usage_pct"].Value
	if next.Status != "ok" || len(next.Metrics) != 1 || v == nil || *v < 0 || *v > 100 {
		t.Fatalf("%+v", next)
	}
}

type blockedModule struct{ release chan struct{} }

func (m blockedModule) sample(context.Context) (sourceSnapshot, error) {
	<-m.release
	return sourceSnapshot{Status: "ok"}, nil
}
func TestModuleTimeoutDoesNotWaitForBlockedRead(t *testing.T) {
	release := make(chan struct{})
	defer close(release)
	start := time.Now()
	out, err, finished := sampleOnce(context.Background(), blockedModule{release}, sourceConfig{Module: "cpu", Fields: []string{"usage_pct"}, timeout: 20 * time.Millisecond})
	if err == nil || out.Status != "error" || time.Since(start) > time.Second {
		t.Fatal("timeout was not isolated")
	}
	select {
	case <-finished:
		t.Fatal("blocked read unexpectedly completed")
	default:
	}
}

func TestV2PublisherRealCPUSecondRound(t *testing.T) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	seen := make(chan bool, 1)
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "POST" || r.URL.Path != "/api/v2/snapshot" {
			t.Error("unexpected request", r.Method, r.URL.Path)
		}
		var snapshot snapshotV2
		if err := json.NewDecoder(r.Body).Decode(&snapshot); err != nil {
			t.Error(err)
		}
		source := snapshot.Sources["system.cpu"]
		if source.Status == "ok" && source.Metrics["usage_pct"].Value != nil {
			select {
			case seen <- true:
			default:
			}
			cancel()
		}
		w.Header().Set("Content-Type", "application/json")
		w.Write([]byte(`{"ok":true}`))
	}))
	defer server.Close()
	body := fmt.Sprintf(`{"version":1,"host_id":"test-cpu","endpoint":%q,"token_file":"token.txt","allow_http_loopback":true,"publish_interval":"30ms","sources":{"system.cpu":{"module":"cpu","interval":"20ms","fields":["usage_pct"]}}}`, server.URL)
	p := testLocalConfig(t, body)
	if err := os.WriteFile(filepath.Join(filepath.Dir(p), "token.txt"), []byte("test-collector-token"), 0600); err != nil {
		t.Fatal(err)
	}
	if err := run(ctx, []string{"--config", p}, &bytes.Buffer{}, &bytes.Buffer{}); err != nil {
		t.Fatal(err)
	}
	select {
	case <-seen:
	default:
		t.Fatal("publisher never sent real CPU second sample")
	}
}

func TestCPUCounterRollbackRequiresWarmup(t *testing.T) {
	previous := cpu.TimesStat{User: 10, Idle: 20, Iowait: 5}
	for _, current := range []cpu.TimesStat{{User: 100, Idle: 19, Iowait: 5}, {User: 100, Idle: 30, Iowait: 4}, previous} {
		if _, valid := cpuUsage(current, previous); valid {
			t.Fatal("invalid counters accepted", current)
		}
	}
	if usage, valid := cpuUsage(cpu.TimesStat{User: 20, Idle: 30, Iowait: 5}, previous); !valid || usage != 50 {
		t.Fatal(usage, valid)
	}
}

func TestInvalidCPUCounterRequiresFreshBaseline(t *testing.T) {
	valid := cpu.TimesStat{User: 10, Idle: 20}
	for _, n := range []float64{math.NaN(), math.Inf(1), math.Inf(-1), -1} {
		invalid := cpu.TimesStat{User: n, Idle: 10}
		if _, ok := cpuUsage(valid, invalid); ok {
			t.Fatalf("accepted invalid baseline %v", n)
		}
		if _, ok := cpuUsage(invalid, valid); ok {
			t.Fatalf("accepted invalid sample %v", n)
		}
	}
	if usage, ok := cpuUsage(cpu.TimesStat{User: 20, Idle: 30}, valid); !ok || usage != 50 {
		t.Fatal(usage, ok)
	}
}
func TestTimeoutReadersExitAfterCancellation(t *testing.T) {
	baseline := runtime.NumGoroutine()
	for i := 0; i < 40; i++ {
		ctx, cancel := context.WithCancel(context.Background())
		done := make(chan struct{})
		module := cancelledModule{done: done}
		cancel()
		_, _, finished := sampleOnce(ctx, module, sourceConfig{Module: "cpu", timeout: time.Second})
		select {
		case <-finished:
		case <-time.After(time.Second):
			t.Fatal("reader leaked after cancellation")
		}
		select {
		case <-done:
		default:
			t.Fatal("reader did not observe cancellation")
		}
	}
	if count := runtime.NumGoroutine(); count > baseline+2 {
		t.Fatalf("goroutines grew from %d to %d", baseline, count)
	}
}

type cancelledModule struct{ done chan struct{} }

func (m cancelledModule) sample(ctx context.Context) (sourceSnapshot, error) {
	<-ctx.Done()
	close(m.done)
	return sourceSnapshot{}, ctx.Err()
}
