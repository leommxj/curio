package main

import (
	"context"
	"fmt"
	"io"

	"golang.org/x/sys/windows/svc"
	"golang.org/x/sys/windows/svc/eventlog"
)

const serviceName = "curio-collector"

type serviceLog struct{ *eventlog.Log }

func (log serviceLog) Write(p []byte) (int, error) {
	return len(p), log.Error(1, string(p))
}

type collectorService struct {
	args []string
	log  io.Writer
}

func serviceConfig(args []string, output io.Writer) (config, error) {
	cfg, err := configFlags(args, "curio-collector", false, output)
	if err == nil {
		cfg, err = loadLocalConfig(cfg)
	}
	if err == nil {
		err = checkConfig(cfg)
	}
	if err == nil {
		err = cfg.prepareIdentity()
	}
	return cfg, err
}

func (s *collectorService) Execute(_ []string, requests <-chan svc.ChangeRequest, changes chan<- svc.Status) (bool, uint32) {
	changes <- svc.Status{State: svc.StartPending, WaitHint: 10000}
	cfg, err := serviceConfig(s.args, s.log)
	if err != nil {
		fmt.Fprintln(s.log, err)
		return true, 1
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() { done <- runCollector(ctx, cfg, io.Discard, s.log) }()
	status := svc.Status{State: svc.Running, Accepts: svc.AcceptStop | svc.AcceptShutdown}
	changes <- status
	for {
		select {
		case err := <-done:
			if err != nil {
				fmt.Fprintln(s.log, err)
				return true, 1
			}
			return false, 0
		case request := <-requests:
			switch request.Cmd {
			case svc.Stop, svc.Shutdown:
				status = svc.Status{State: svc.StopPending, WaitHint: 10000}
				changes <- status
				cancel()
			case svc.Interrogate:
				changes <- status
			}
		}
	}
}
