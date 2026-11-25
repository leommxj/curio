package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"strings"
	"testing"
	"time"

	gnet "github.com/shirou/gopsutil/v4/net"
)

func TestNetworkSamples(t *testing.T) {
	at := time.Now()
	counters := []gnet.IOCountersStat{{Name: "eth-test", BytesRecv: 1 << 63, BytesSent: 100}}
	m := localModule{config: sourceConfig{Module: "network", Interface: "eth-test", Fields: []string{"rx_bytes_per_second", "tx_bytes_per_second"}}, readNetwork: func(context.Context, bool) ([]gnet.IOCountersStat, error) { return counters, nil }, now: func() time.Time { return at }}
	first, err := m.sample(context.Background())
	if err != nil || first.Status != "warming_up" {
		t.Fatal(first, err)
	}
	at = at.Add(2 * time.Second)
	counters[0].BytesRecv += 10
	counters[0].BytesSent += 20
	next, err := m.sample(context.Background())
	if err != nil || *next.Metrics["rx_bytes_per_second"].Value != 5 || *next.Metrics["tx_bytes_per_second"].Value != 10 {
		t.Fatal(next, err)
	}
	at = at.Add(time.Second)
	counters[0].BytesSent = 1
	reset, err := m.sample(context.Background())
	if err != nil || reset.Status != "warming_up" {
		t.Fatal(reset, err)
	}
	counters[0].Name = "renamed"
	if _, err = m.sample(context.Background()); err == nil || m.network != nil {
		t.Fatal("missing NIC retained baseline")
	}
	counters[0].Name = "eth-test"
	again, err := m.sample(context.Background())
	if err != nil || again.Status != "warming_up" {
		t.Fatal(again, err)
	}
	same := networkSample{at: at}
	if _, _, ok := networkRates(same, &same); ok {
		t.Fatal("zero interval accepted")
	}
}

func TestSourceMaxAgeAndOptions(t *testing.T) {
	for _, options := range []string{`module: disk`, `module: network`, "module: cpu\n    path: /", "module: cpu\n    cli: false", "module: cpu\n    unknown: true", "module: memory\n    interface: eth0", "module: network\n    interface: eth0\n    cli: true", "module: cpu\n    max_age: 1ns", "module: cpu\n    max_age: 1s", "module: cpu\n    max_age: 169h", "module: cpu\n    interval: 999999h"} {
		path := testLocalConfig(t, "sources:\n  test:\n    "+options+"\n")
		if _, err := parseConfig([]string{"--config", path}, &bytes.Buffer{}); err == nil {
			t.Fatalf("accepted %s", options)
		}
	}
	for _, interval := range []string{"2s", "4s"} {
		p := testLocalConfig(t, fmt.Sprintf("sources:\n  test:\n    module: cpu\n    interval: %s\n", interval))
		cfg, err := parseConfig([]string{"--config", p}, &bytes.Buffer{})
		if err != nil {
			t.Fatal(err)
		}
		s := cfg.Local.Sources["test"]
		expected := 10 * time.Second
		if interval == "4s" {
			expected = 12 * time.Second
		}
		for _, out := range []sourceSnapshot{declaration(s), failure(s)} {
			data, err := json.Marshal(out)
			if err != nil || !strings.Contains(string(data), fmt.Sprintf(`"max_age_ms":%d`, expected.Milliseconds())) {
				t.Fatal(string(data), err)
			}
		}
	}
}

func TestDiskConfigAndNetworkDryRun(t *testing.T) {
	p := testLocalConfig(t, "sources:\n  volume:\n    module: disk\n    path: .\n  nic:\n    module: network\n    interface: curio-nonexistent-nic\n")
	cfg, err := parseConfig([]string{"--config", p}, &bytes.Buffer{})
	if err != nil {
		t.Fatal(err)
	}
	if cfg.Local.Sources["volume"].Path == "." || len(cfg.Local.Sources["volume"].Fields) != 4 {
		t.Fatal(cfg.Local)
	}
	cfg.DryRun = true
	var output bytes.Buffer
	_ = runCollector(context.Background(), cfg, &output, &bytes.Buffer{})
	var wire snapshotV2
	if err = json.Unmarshal(output.Bytes(), &wire); err != nil {
		t.Fatal(err, output.String())
	}
	disk := wire.Sources["volume"]
	if disk.Status != "ok" || len(disk.Metrics) != 4 || disk.Metrics["free_bytes"].Value == nil {
		t.Fatal(output.String())
	}
	nic := wire.Sources["nic"]
	if nic.Status != "error" || nic.MaxAge != 10000 || len(nic.Metrics) != 2 || nic.Metrics["rx_bytes_per_second"].Unit != "bytes_per_second" {
		t.Fatal(output.String())
	}
}

func TestExportDefaultMaxAge(t *testing.T) {
	p := testLocalConfig(t, "sources:\n  nic:\n    module: network\n    interface: eth0\n    interval: 4s\n    max_age: 12s\n    fields: [rx_bytes_per_second, tx_bytes_per_second]\n")
	var output bytes.Buffer
	if err := exportConfig(p, &output, false); err != nil {
		t.Fatal(err)
	}
	if strings.Contains(output.String(), "max_age") || strings.Contains(output.String(), "fields") {
		t.Fatal(output.String())
	}
}

func TestDefaultMaxAgeRoundsUp(t *testing.T) {
	p := testLocalConfig(t, "sources:\n  cpu:\n    module: cpu\n    interval: 4.0001s\n")
	cfg, err := parseConfig([]string{"--config", p}, &bytes.Buffer{})
	if err != nil {
		t.Fatal(err)
	}
	source := cfg.Local.Sources["cpu"]
	if source.maxAge != 12001*time.Millisecond {
		t.Fatal(source.maxAge)
	}
	explicit := testLocalConfig(t, "sources:\n  cpu:\n    module: cpu\n    interval: 4.0001s\n    max_age: 12001ms\n")
	var output bytes.Buffer
	if err := exportConfig(explicit, &output, false); err != nil {
		t.Fatal(err)
	}
	if strings.Contains(output.String(), "max_age") {
		t.Fatal(output.String())
	}
	roundTrip := testLocalConfig(t, output.String())
	normalized, err := parseConfig([]string{"--config", roundTrip}, &bytes.Buffer{})
	if err != nil || normalized.Local.Sources["cpu"].maxAge != source.maxAge {
		t.Fatal(normalized, err)
	}
	if age := defaultMaxAge(time.Duration(1<<63 - 1)); age <= 7*24*time.Hour {
		t.Fatal("overflowed default", age)
	}
}
