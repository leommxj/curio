package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"sync"
	"time"
)

type snapshotV2 struct {
	Version  int                       `json:"v"`
	HostID   string                    `json:"host_id"`
	HostName string                    `json:"host_name"`
	Sources  map[string]sourceSnapshot `json:"sources"`
}
type collection struct {
	mu       sync.Mutex
	latest   map[string]sourceSnapshot
	failures map[string]error
}

func (c *collection) update(id string, s sourceSnapshot, err error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.latest[id] = s
	c.failures[id] = err
}
func (c *collection) encode(cfg config) ([]byte, error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	snapshot := snapshotV2{2, cfg.HostID, cfg.HostName, make(map[string]sourceSnapshot, len(c.latest))}
	agentCount := 0
	for id, s := range c.latest {
		if s.Agents != nil {
			agentCount += len(*s.Agents)
		}
		if !s.sampled.IsZero() {
			s.Age = time.Since(s.sampled).Milliseconds()
			if s.Age < 0 {
				s.Age = 0
			}
		}
		snapshot.Sources[id] = s
	}
	if agentCount > 16 {
		return nil, fmt.Errorf("%d agents exceed host capacity 16", agentCount)
	}
	data, err := json.Marshal(snapshot)
	if err != nil {
		return nil, err
	}
	if len(data) > maxSnapshotBytes {
		return nil, fmt.Errorf("snapshot exceeds %d bytes", maxSnapshotBytes)
	}
	return data, nil
}
func (c *collection) sourceError() error {
	c.mu.Lock()
	defer c.mu.Unlock()
	for id, err := range c.failures {
		if err != nil {
			return fmt.Errorf("source %s: %w", id, err)
		}
	}
	return nil
}
func sampleOnce(ctx context.Context, m sampleModule, s sourceConfig) (sourceSnapshot, error, <-chan struct{}) {
	readCtx, cancel := context.WithTimeout(ctx, s.timeout)
	defer cancel()
	// A stalled module has at most one in-flight call; its worker does not queue more work.
	type result struct {
		s   sourceSnapshot
		err error
	}
	done := make(chan result, 1)
	finished := make(chan struct{})
	go func() { out, err := m.sample(readCtx); done <- result{out, err}; close(finished) }()
	select {
	case r := <-done:
		if r.err != nil {
			return failure(s), r.err, finished
		}
		return r.s, nil, finished
	case <-readCtx.Done():
		return failure(s), readCtx.Err(), finished
	}
}
func runCollector(ctx context.Context, cfg config, stdout, stderr io.Writer) error {
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	var hub *poster
	var err error
	if !cfg.DryRun {
		if err = cfg.loadToken(); err != nil {
			return err
		}
		hub, err = newPoster(cfg)
		if err != nil {
			return err
		}
		defer hub.close()
	}
	c := &collection{latest: map[string]sourceSnapshot{}, failures: map[string]error{}}
	var ready sync.WaitGroup
	for id, s := range cfg.Local.Sources {
		c.update(id, declaration(s), nil)
		ready.Add(1)
		go func(id string, s sourceConfig) {
			m := &localModule{config: s}
			out, err, finished := sampleOnce(ctx, m, s)
			c.update(id, out, err)
			ready.Done()
			if cfg.Once || cfg.DryRun {
				return
			}
			select {
			case <-ctx.Done():
				return
			case <-finished:
			}
			timer := time.NewTicker(s.interval)
			defer timer.Stop()
			for {
				select {
				case <-ctx.Done():
					return
				case <-timer.C:
					out, err, finished = sampleOnce(ctx, m, s)
					c.update(id, out, err)
					select {
					case <-ctx.Done():
						return
					case <-finished:
					}
				}
			}
		}(id, s)
	}
	ready.Wait()
	for {
		data, err := c.encode(cfg)
		if err == nil {
			if cfg.DryRun {
				_, err = fmt.Fprintln(stdout, string(data))
			} else {
				postCtx, stop := context.WithTimeout(ctx, cfg.Timeout)
				err = hub.post(postCtx, data)
				stop()
			}
		}
		if cfg.Once || cfg.DryRun {
			return errors.Join(err, c.sourceError())
		}
		if err != nil && ctx.Err() == nil {
			fmt.Fprintln(stderr, "collector:", err)
		}
		timer := time.NewTimer(cfg.Interval)
		select {
		case <-ctx.Done():
			timer.Stop()
			return nil
		case <-timer.C:
		}
	}
}
