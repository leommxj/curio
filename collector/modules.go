package main

import (
	"context"
	"encoding/json"
	"errors"
	"math"
	"runtime"
	"time"

	"github.com/shirou/gopsutil/v4/cpu"
	"github.com/shirou/gopsutil/v4/disk"
	"github.com/shirou/gopsutil/v4/mem"
	gnet "github.com/shirou/gopsutil/v4/net"
)

type metric struct {
	Unit  string   `json:"unit"`
	Value *float64 `json:"value,omitempty"`
}
type sourceSnapshot struct {
	Kind    string            `json:"kind"`
	Status  string            `json:"status"`
	MaxAge  int64             `json:"max_age_ms"`
	Age     int64             `json:"sample_age_ms"`
	Metrics map[string]metric `json:"metrics,omitempty"`
	Agents  *[]Agent          `json:"agents,omitempty"`
	sampled time.Time
}
type sampleModule interface {
	sample(context.Context) (sourceSnapshot, error)
}
type localModule struct {
	config      sourceConfig
	previous    *cpu.TimesStat
	network     *networkSample
	readNetwork func(context.Context, bool) ([]gnet.IOCountersStat, error)
	now         func() time.Time
}

func value(unit string, n float64) metric { return metric{unit, &n} }
func declaration(s sourceConfig) sourceSnapshot {
	out := sourceSnapshot{Kind: "metrics", Status: "warming_up", MaxAge: s.maxAge.Milliseconds(), Metrics: map[string]metric{}}
	if s.Module == "herdr" {
		agents := []Agent{}
		out.Kind = "agents"
		out.Agents = &agents
		out.Metrics = nil
	} else {
		for _, key := range s.Fields {
			out.Metrics[key] = metric{Unit: moduleFields[s.Module][key]}
		}
	}
	return out
}
func listSet(items []string) map[string]bool {
	out := map[string]bool{}
	for _, s := range items {
		out[s] = true
	}
	return out
}
func cpuTotal(t cpu.TimesStat) float64 {
	return t.User + t.System + t.Idle + t.Nice + t.Iowait + t.Irq + t.Softirq + t.Steal
}
func cpuUsage(current, previous cpu.TimesStat) (float64, bool) {
	now := []float64{current.User, current.System, current.Idle, current.Nice, current.Iowait, current.Irq, current.Softirq, current.Steal}
	before := []float64{previous.User, previous.System, previous.Idle, previous.Nice, previous.Iowait, previous.Irq, previous.Softirq, previous.Steal}
	for i, n := range now {
		if math.IsNaN(n) || math.IsInf(n, 0) || math.IsNaN(before[i]) || math.IsInf(before[i], 0) || before[i] < 0 || n < before[i] {
			return 0, false
		}
	}
	total := cpuTotal(current) - cpuTotal(previous)
	idle := current.Idle + current.Iowait - previous.Idle - previous.Iowait
	if total <= 0 || idle < 0 || idle > total {
		return 0, false
	}
	return math.Max(0, math.Min(100, 100*(total-idle)/total)), true
}

func (m *localModule) sample(ctx context.Context) (sourceSnapshot, error) {
	s := m.config
	out := declaration(s)
	out.sampled = time.Now()
	out.Status = "ok"
	switch s.Module {
	case "herdr":
		agents, err := (herdrSource{Socket: s.Socket, Session: s.Session, Binary: s.Binary}).read(ctx)
		if err != nil {
			return out, err
		}
		mapped, err := mapAgents(agents, mapOptions{Projects: listSet(s.AllowProjects), Brands: listSet(s.AllowAgents), Panes: listSet(s.AllowPanes), Aliases: s.Aliases, ProjectMode: s.Project, AllowTitles: s.AllowTitles})
		if err != nil {
			return out, err
		}
		out.Agents = &mapped
	case "cpu":
		times, err := cpu.TimesWithContext(ctx, false)
		if err != nil {
			return out, err
		}
		if len(times) != 1 {
			return out, errors.New("CPU counters unavailable")
		}
		current := times[0]
		previous := m.previous
		m.previous = &current
		if previous == nil {
			out.Status = "warming_up"
			return out, nil
		}
		usage, valid := cpuUsage(current, *previous)
		if !valid {
			out.Status = "warming_up"
			return out, nil
		}
		count, err := cpu.CountsWithContext(ctx, true)
		if err != nil {
			return out, err
		}
		out.Metrics = map[string]metric{"usage_pct": value("percent", usage), "logical_count": value("count", float64(count))}
	case "disk":
		usage, err := disk.UsageWithContext(ctx, s.Path)
		if err != nil {
			return out, err
		}
		if usage.Total == 0 || usage.Used > usage.Total || usage.Free > usage.Total || usage.Total > 9007199254740991 || math.IsNaN(usage.UsedPercent) || math.IsInf(usage.UsedPercent, 0) || usage.UsedPercent < 0 || usage.UsedPercent > 100 {
			return out, errors.New("invalid disk counters")
		}
		out.Metrics = map[string]metric{"usage_pct": value("percent", usage.UsedPercent), "used_bytes": value("bytes", float64(usage.Used)), "free_bytes": value("bytes", float64(usage.Free)), "total_bytes": value("bytes", float64(usage.Total))}
	case "network":
		read := m.readNetwork
		if read == nil {
			read = gnet.IOCountersWithContext
		}
		counters, err := read(ctx, true)
		if err != nil {
			m.network = nil
			return out, err
		}
		now := time.Now()
		if m.now != nil {
			now = m.now()
		}
		current, err := networkCounter(counters, s.Interface, now)
		if err != nil {
			m.network = nil
			return out, err
		}
		previous := m.network
		m.network = &current
		rx, tx, valid := networkRates(current, previous)
		if !valid {
			out.Status = "warming_up"
			return out, nil
		}
		out.Metrics = map[string]metric{"rx_bytes_per_second": value("bytes_per_second", rx), "tx_bytes_per_second": value("bytes_per_second", tx)}
	case "memory":
		memory, err := mem.VirtualMemoryWithContext(ctx)
		if err != nil {
			return out, err
		}
		if memory.Total == 0 || memory.Available > memory.Total || memory.Total > 9007199254740991 {
			return out, errors.New("invalid memory counters")
		}
		used := memory.Total - memory.Available
		out.Metrics = map[string]metric{"usage_pct": value("percent", 100*float64(used)/float64(memory.Total)), "used_bytes": value("bytes", float64(used)), "total_bytes": value("bytes", float64(memory.Total)), "available_bytes": value("bytes", float64(memory.Available))}
	}
	if out.Metrics != nil {
		filtered := map[string]metric{}
		for _, key := range s.Fields {
			filtered[key] = out.Metrics[key]
		}
		out.Metrics = filtered
	}
	out.sampled = time.Now()
	return out, nil
}
func failure(s sourceConfig) sourceSnapshot {
	out := declaration(s)
	out.Status = "error"
	out.sampled = time.Now()
	if runtime.GOOS != "linux" && runtime.GOOS != "windows" && runtime.GOOS != "darwin" && s.Module != "herdr" {
		out.Status = "unavailable"
	}
	return out
}

func (s sourceSnapshot) MarshalJSON() ([]byte, error) {
	common := map[string]any{"kind": s.Kind, "status": s.Status, "sample_age_ms": s.Age, "max_age_ms": s.MaxAge}
	if s.Kind == "agents" {
		common["agents"] = s.Agents
	} else {
		common["metrics"] = s.Metrics
	}
	return json.Marshal(common)
}

type networkSample struct {
	rx, tx uint64
	at     time.Time
}

func networkCounter(counters []gnet.IOCountersStat, name string, at time.Time) (networkSample, error) {
	for _, c := range counters {
		if c.Name == name {
			return networkSample{c.BytesRecv, c.BytesSent, at}, nil
		}
	}
	return networkSample{}, errors.New("network interface unavailable: " + name)
}
func networkRates(current networkSample, previous *networkSample) (float64, float64, bool) {
	if previous == nil || current.rx < previous.rx || current.tx < previous.tx {
		return 0, 0, false
	}
	elapsed := current.at.Sub(previous.at).Seconds()
	if elapsed <= 0 {
		return 0, 0, false
	}
	return float64(current.rx-previous.rx) / elapsed, float64(current.tx-previous.tx) / elapsed, true
}
