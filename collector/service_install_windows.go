package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"

	"golang.org/x/sys/windows"
	"golang.org/x/sys/windows/svc"
	"golang.org/x/sys/windows/svc/eventlog"
	"golang.org/x/sys/windows/svc/mgr"
)

func manageService(args []string) error {
	if len(args) == 0 || (args[0] != "install" && args[0] != "uninstall" && args[0] != "config") {
		return errors.New("usage: service install [--config PATH] | service uninstall | service config")
	}
	if args[0] != "install" && len(args) != 1 {
		return fmt.Errorf("service %s takes no options", args[0])
	}
	if args[0] == "config" {
		path, err := installedConfigPath()
		if err == nil {
			fmt.Fprintln(os.Stdout, path)
		}
		return err
	}
	m, err := mgr.Connect()
	if err != nil {
		return fmt.Errorf("open service manager (administrator required): %w", err)
	}
	defer m.Disconnect()
	s, err := m.OpenService(serviceName)
	if err != nil && !errors.Is(err, windows.ERROR_SERVICE_DOES_NOT_EXIST) {
		return err
	}
	if s != nil {
		defer s.Close()
	}
	if args[0] == "install" {
		return installService(m, s, args[1:])
	}
	if s == nil {
		return nil
	}
	if err = setServiceState(s, svc.Stopped); err != nil {
		return err
	}
	if err = s.Delete(); err != nil {
		return err
	}
	return removeServiceLog()
}

func installService(m *mgr.Mgr, s *mgr.Service, args []string) (err error) {
	registration := mgr.Config{DisplayName: "Curio Collector", StartType: mgr.StartAutomatic, ServiceStartName: `NT AUTHORITY\LocalService`}
	var target, previousPath string
	if s != nil {
		registration, err = s.Config()
		if err != nil {
			return err
		}
		target, previousPath, err = servicePaths(registration.BinaryPathName)
		if err != nil {
			return err
		}
		if len(args) == 0 {
			args = []string{"--config", previousPath}
		}
	} else {
		directory, err := windows.KnownFolderPath(windows.FOLDERID_ProgramFiles, 0)
		if err != nil {
			return err
		}
		target = filepath.Join(directory, "Curio", "curio-collector.exe")
	}
	cfg, err := serviceConfig(args, os.Stderr)
	if err != nil {
		return err
	}
	sid, err := serviceAccountSID(registration.ServiceStartName)
	if err != nil {
		return fmt.Errorf("resolve service account %s: %w", registration.ServiceStartName, err)
	}
	system, err := windows.GetSystemDirectory()
	if err != nil {
		return err
	}
	files := []string{cfg.ConfigFile, cfg.CA, cfg.TokenFile}
	if cfg.Local.HostID == "" {
		files = append(files, cfg.IdentityFile)
	}
	for _, path := range files {
		if path != "" {
			if output, err := exec.Command(filepath.Join(system, "icacls.exe"), path, "/grant", "*"+sid.String()+":R").CombinedOutput(); err != nil {
				return fmt.Errorf("grant service account read access: %w: %s", err, output)
			}
		}
	}
	executable, err := os.Executable()
	if err != nil {
		return err
	}
	if s != nil {
		var status svc.Status
		status, err = s.Query()
		if err != nil {
			return err
		}
		if err = setServiceState(s, svc.Stopped); err != nil {
			return err
		}
		defer func() {
			if err != nil && status.State == svc.Running {
				err = errors.Join(err, setServiceState(s, svc.Running))
			}
		}()
	}
	if err = replaceServiceBinary(executable, target); err != nil {
		return err
	}
	if s == nil {
		s, err = m.CreateService(serviceName, target, registration, "--config", cfg.ConfigFile)
		if err != nil {
			return err
		}
		defer s.Close()
		if err = removeServiceLog(); err != nil {
			return errors.Join(err, s.Delete())
		}
		if err = eventlog.InstallAsEventCreate(serviceName, eventlog.Error); err != nil {
			return errors.Join(err, s.Delete())
		}
	} else if cfg.ConfigFile != previousPath {
		command, err := windows.UTF16PtrFromString(windows.ComposeCommandLine([]string{target, "--config", cfg.ConfigFile}))
		if err != nil {
			return err
		}
		if err = windows.ChangeServiceConfig(s.Handle, windows.SERVICE_NO_CHANGE, windows.SERVICE_NO_CHANGE,
			windows.SERVICE_NO_CHANGE, command, nil, nil, nil, nil, nil, nil); err != nil {
			return err
		}
	}
	if err = s.SetRecoveryActions([]mgr.RecoveryAction{
		{Type: mgr.ServiceRestart, Delay: 5 * time.Second},
		{Type: mgr.ServiceRestart, Delay: 30 * time.Second},
		{Type: mgr.NoAction},
	}, 60); err != nil {
		return err
	}
	if err = s.SetRecoveryActionsOnNonCrashFailures(true); err != nil {
		return err
	}
	if registration.StartType == mgr.StartDisabled {
		return nil
	}
	return setServiceState(s, svc.Running)
}

func servicePaths(command string) (string, string, error) {
	args, err := windows.DecomposeCommandLine(command)
	if err != nil || len(args) < 2 {
		return "", "", errors.New("service command must specify an executable and --config")
	}
	flags := flag.NewFlagSet(serviceName, flag.ContinueOnError)
	flags.SetOutput(io.Discard)
	path := flags.String("config", "", "")
	if err = flags.Parse(args[1:]); err != nil {
		return "", "", fmt.Errorf("invalid service command: %v", err)
	}
	if flags.NArg() != 0 || !filepath.IsAbs(args[0]) || !filepath.IsAbs(*path) {
		return "", "", errors.New("service executable and configuration paths must be absolute")
	}
	return args[0], *path, nil
}

func serviceAccountSID(account string) (*windows.SID, error) {
	switch strings.ToLower(account) {
	case "localsystem":
		return windows.StringToSid("S-1-5-18")
	case `nt authority\localservice`:
		return windows.StringToSid("S-1-5-19")
	case `nt authority\networkservice`:
		return windows.StringToSid("S-1-5-20")
	}
	sid, _, _, err := windows.LookupSID("", account)
	return sid, err
}

func installedConfigPath() (string, error) {
	manager, err := windows.OpenSCManager(nil, nil, windows.SC_MANAGER_CONNECT)
	if err != nil {
		return "", err
	}
	defer windows.CloseServiceHandle(manager)
	name, _ := windows.UTF16PtrFromString(serviceName)
	handle, err := windows.OpenService(manager, name, windows.SERVICE_QUERY_CONFIG)
	if errors.Is(err, windows.ERROR_SERVICE_DOES_NOT_EXIST) {
		cfg, err := configFlags(nil, "curio-collector", false, io.Discard)
		return cfg.ConfigFile, err
	}
	if err != nil {
		return "", err
	}
	defer windows.CloseServiceHandle(handle)
	registration, err := (&mgr.Service{Handle: handle}).Config()
	if err != nil {
		return "", err
	}
	_, path, err := servicePaths(registration.BinaryPathName)
	return path, err
}

func replaceServiceBinary(source, target string) error {
	if strings.EqualFold(source, target) {
		return nil
	}
	data, err := os.ReadFile(source)
	if err != nil {
		return err
	}
	if err = os.MkdirAll(filepath.Dir(target), 0755); err != nil {
		return err
	}
	file, err := os.CreateTemp(filepath.Dir(target), ".curio-*.exe")
	if err != nil {
		return err
	}
	defer os.Remove(file.Name())
	_, err = file.Write(data)
	if err = errors.Join(err, file.Close()); err != nil {
		return err
	}
	return os.Rename(file.Name(), target)
}

func removeServiceLog() error {
	err := eventlog.Remove(serviceName)
	if errors.Is(err, windows.ERROR_FILE_NOT_FOUND) {
		return nil
	}
	return err
}

func setServiceState(s *mgr.Service, state svc.State) error {
	var process windows.Handle
	defer func() {
		if process != 0 {
			windows.CloseHandle(process)
		}
	}()
	if state == svc.Running {
		if err := s.Start(); err != nil && !errors.Is(err, windows.ERROR_SERVICE_ALREADY_RUNNING) {
			return err
		}
	}
	for deadline := time.Now().Add(15 * time.Second); time.Now().Before(deadline); time.Sleep(100 * time.Millisecond) {
		status, err := s.Query()
		if err != nil {
			return err
		}
		if state == svc.Stopped && process == 0 && status.ProcessId != 0 {
			process, err = windows.OpenProcess(windows.SYNCHRONIZE, false, status.ProcessId)
			if err != nil && !errors.Is(err, windows.ERROR_INVALID_PARAMETER) {
				return err
			}
		}
		if status.State == state {
			if process != 0 {
				result, err := windows.WaitForSingleObject(process, 15000)
				if err != nil {
					return err
				}
				if result != windows.WAIT_OBJECT_0 {
					return errors.New("service process did not exit within 15 seconds")
				}
			}
			return nil
		}
		if state == svc.Running && status.State == svc.Stopped {
			return fmt.Errorf("service stopped with exit code %d/%d; see the Application event log", status.Win32ExitCode, status.ServiceSpecificExitCode)
		}
		if state == svc.Stopped && status.State == svc.Running {
			if _, err = s.Control(svc.Stop); err != nil && !errors.Is(err, windows.ERROR_SERVICE_NOT_ACTIVE) {
				return err
			}
		}
	}
	return errors.New("service did not change state within 15 seconds")
}
