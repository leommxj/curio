package main

import (
	"bufio"
	"bytes"
	"context"
	"errors"
	"io"
	"net"
	"os/exec"
	"time"
)

const maxSourceBytes = 4 * 1024 * 1024

type herdrSource struct {
	Socket  string
	Session string
	Binary  string
}

func (source herdrSource) read(ctx context.Context) ([]nativeAgent, error) {
	var data []byte
	var err error
	if source.Socket != "" {
		data, err = readSocket(ctx, source.Socket)
	} else {
		args := []string{"agent", "list"}
		if source.Session != "" {
			args = append([]string{"--session", source.Session}, args...)
		}
		command := exec.CommandContext(ctx, source.Binary, args...)
		command.WaitDelay = 100 * time.Millisecond
		var output limitedBuffer
		command.Stdout = &output
		command.Stderr = io.Discard
		err = command.Run()
		data = output.Bytes()
	}
	if err != nil {
		return nil, errors.New("cannot query herdr agent.list")
	}
	return parseAgents(data)
}

type limitedBuffer struct{ bytes.Buffer }

func (buffer *limitedBuffer) Write(p []byte) (int, error) {
	if buffer.Len()+len(p) > maxSourceBytes {
		return 0, errors.New("herdr response too large")
	}
	return buffer.Buffer.Write(p)
}

func readSocket(ctx context.Context, socket string) ([]byte, error) {
	conn, err := (&net.Dialer{}).DialContext(ctx, "unix", socket)
	if err != nil {
		return nil, err
	}
	defer conn.Close()
	if deadline, ok := ctx.Deadline(); ok {
		if err := conn.SetDeadline(deadline); err != nil {
			return nil, err
		}
	}
	stop := context.AfterFunc(ctx, func() { conn.Close() })
	defer stop()
	if _, err := io.WriteString(conn, "{\"id\":\"curio-agent-list\",\"method\":\"agent.list\",\"params\":{}}\n"); err != nil {
		return nil, err
	}
	reader := bufio.NewReader(io.LimitReader(conn, maxSourceBytes+1))
	data, err := reader.ReadBytes('\n')
	if err != nil && !(err == io.EOF && len(data) > 0) {
		return nil, err
	}
	if len(data) > maxSourceBytes {
		return nil, errors.New("herdr response too large")
	}
	return data, nil
}
