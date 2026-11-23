package main

import (
	"context"
	_ "embed"
	"encoding/base64"
	"encoding/binary"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"syscall"
	"unicode/utf16"

	"golang.org/x/sys/windows"
	"golang.org/x/sys/windows/svc"
	"golang.org/x/sys/windows/svc/eventlog"
)

//go:embed window_windows.ps1
var windowScript string

func launch(ctx context.Context, args []string) error {
	service, err := svc.IsWindowsService()
	if err != nil {
		return err
	}
	if service {
		log, err := eventlog.Open(serviceName)
		if err != nil {
			return err
		}
		defer log.Close()
		return svc.Run(serviceName, &collectorService{args, serviceLog{log}})
	}
	if len(args) > 0 && args[0] == "service" {
		return manageService(args[1:])
	}
	if len(args) != 0 {
		return run(ctx, args, os.Stdout, os.Stderr)
	}
	if err := openWindow(ctx); err != nil {
		message, _ := windows.UTF16PtrFromString(err.Error())
		title, _ := windows.UTF16PtrFromString("Curio Collector")
		windows.MessageBox(0, message, title, windows.MB_OK|windows.MB_ICONERROR)
		return err
	}
	return nil
}

func openWindow(ctx context.Context) error {
	executable, err := os.Executable()
	if err != nil {
		return err
	}
	system, err := windows.GetSystemDirectory()
	if err != nil {
		return err
	}
	script := utf16.Encode([]rune(windowScript))
	encoded := make([]byte, len(script)*2)
	for i, ch := range script {
		binary.LittleEndian.PutUint16(encoded[i*2:], ch)
	}
	cmd := exec.CommandContext(ctx, filepath.Join(system, "WindowsPowerShell", "v1.0", "powershell.exe"),
		"-NoProfile", "-STA", "-NonInteractive", "-EncodedCommand", base64.StdEncoding.EncodeToString(encoded))
	cmd.Env = append(os.Environ(), "CURIO_COLLECTOR_EXE="+executable)
	cmd.SysProcAttr = &syscall.SysProcAttr{CreationFlags: windows.CREATE_NO_WINDOW}
	windows.NewLazySystemDLL("kernel32.dll").NewProc("FreeConsole").Call()
	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("Cannot open Collector window: %w\n%s", err, strings.TrimSpace(string(output)))
	}
	return nil
}
