# Collector

`curio-collector` runs collection modules and posts HTTPS snapshots to the Hub. Its local YAML file selects what to collect; Hub configuration selects what to display. See the [protocol](protocol.md) for snapshot fields and limits. Run the commands below from the repository root; see the [README](../README.md) for build instructions.

## Supported modules and platforms

Collector is one executable with built-in modules. Each entry under `sources` configures a module instance with a source ID. For example, `system.cpu` is a source ID using `module: cpu`; Hub bindings use the host and source IDs.

| Category | Module | Snapshot kind | Data |
| --- | --- | --- | --- |
| System metrics | `cpu` | `metrics` | `usage_pct`, `logical_count` |
| System metrics | `memory` | `metrics` | `usage_pct`, `used_bytes`, `total_bytes`, `available_bytes` |
| System metrics | `disk` | `metrics` | `usage_pct`, `used_bytes`, `free_bytes`, `total_bytes` |
| System metrics | `network` | `metrics` | `rx_bytes_per_second`, `tx_bytes_per_second` |
| Application state | `herdr` | `agents` | Agent IDs, names, brands, projects and states |

OS compatibility is a separate property of each module. The same module names and YAML structure apply on all three platforms.

| Module | Linux | Windows | macOS |
| --- | --- | --- | --- |
| `cpu` | Native sampling verified | Native sampling verified on Windows 10 | gopsutil implementation; native sampling unverified |
| `memory` | Native sampling verified | Native sampling verified on Windows 10 | gopsutil implementation; native sampling unverified |
| `disk` | Native sampling verified | Native sampling verified on Windows 10 | gopsutil implementation; native sampling unverified |
| `network` | Native sampling verified | Native sampling verified on Windows 10 | gopsutil implementation has known limitations; native sampling unverified |
| `herdr` | Unix socket by default; CLI optional | CLI by default; native integration unverified | Unix socket by default; CLI optional; native integration unverified |

System metrics use gopsutil's OS-specific implementations. The herdr module requires a running herdr instance and a working socket or CLI on that machine. It reads agent state through herdr; it does not query Codex or Pi directly. Cross-compilation does not verify native collection.

`init`, `check`, `export` and `run` are shared commands on Linux, Windows and macOS. Service deployment supports Linux systemd and Windows services; see [Services](#services).

Windows also has a [setup window](#windows-setup).

Adding a built-in module requires changing and rebuilding Collector. A separate reporter can also send compatible snapshots directly to the Hub. The protocol accepts metrics, status text and agent lists. No built-in module produces generic status text yet. See [screen bindings](protocol.md#screen-configuration) and [alert rules](protocol.md#alerts) for their current constraints.

## Configuration and commands

First set up the Hub through its temporary Wi-Fi page or with `make configure` and `make provision` as described in the [firmware guide](firmware.md#first-setup). The Wi-Fi page downloads `server-cert.pem` and `curio.yaml`; copy `access.collector_token` from the YAML into a private token file for the Collector. Computer setup writes `firmware/local/server-cert.pem` and `firmware/local/token.txt` directly. Use the configured hostname in the Collector endpoint, for example `https://curio.local`, and point `ca` at that certificate. If the Hub hostname is changed later, download its new certificate and update the Collector's endpoint and CA file before reconnecting.

```sh
./build/curio-collector init
# Edit collector.yaml and use the certificate and token from Hub setup.
./build/curio-collector check
./build/curio-collector --dry-run
./build/curio-collector
```

The default configuration is `os.UserConfigDir()/curio/collector.yaml`:

| Platform | Default path |
| --- | --- |
| Linux | `$XDG_CONFIG_HOME/curio/collector.yaml`, or `$HOME/.config/curio/collector.yaml` |
| macOS | `$HOME/Library/Application Support/curio/collector.yaml` |
| Windows | `%AppData%\curio\collector.yaml` |

Use `--config PATH` with any command to select another file.

| Command | Behavior |
| --- | --- |
| `init` | Creates the [example configuration](../collector/config.example.yaml); refuses to overwrite a file |
| `check` | Validates YAML, token, CA and any existing identity file; does not sample, contact the Hub or create an identity |
| `export` | Prints YAML with defaults omitted; preserves file references without reading credentials |
| `--dry-run` | Samples once and prints a snapshot without requiring a Hub or token |
| `--once` | Samples and posts once |
| `run` | Samples and publishes continuously |
| No arguments | Opens the setup window on Windows; collects continuously on Linux/macOS |

Sampling flags such as `--config` and `--dry-run` can also be used without `run` on every platform.

Sampling errors make `--once` and `--dry-run` exit unsuccessfully. Use `--help` to list flags. Configuration changes require a restart.

YAML must contain one mapping, at most 64 KiB. Unknown fields and duplicate keys are rejected. `version` defaults to `1`; no other configuration version is supported. This is separate from snapshot protocol version `2`.

| Setting | Meaning and default |
| --- | --- |
| `endpoint` | HTTPS base URL or full `/api/v2/snapshot` URL |
| `ca` | Optional PEM certificates added to system trust; the server certificate must match the endpoint hostname or IP |
| `token_file` | File containing a 16–128-character collector token (ASCII `!` through `~`, no spaces); required when posting |
| `host_name` | Display name; defaults to the system hostname, limited to 48 UTF-8 bytes |
| `host_id` | Optional explicit identity: 1–48 ASCII letters, digits, `.`, `_` or `-` |
| `identity_file` | Persistent random host ID; defaults to `host-id` beside the YAML file |
| `publish_interval` | Publication interval, default `2s` |
| `timeout` | Posting timeout and default source timeout, default `5s` |
| `allow_http_loopback` | Default `false`; permits development HTTP only for `localhost` or a loopback IP |
| `sources` | Required mapping of source IDs to module settings; `{}` collects no sources |

Relative file paths resolve from the YAML directory. Paths do not expand `~` or environment variables. A `binary` without a directory is found through `PATH`; one with a directory resolves from the YAML directory. Moving an exported configuration changes the base for relative paths; prepare its certificate, token and identity files separately before running `check`.

The first collection creates the identity file unless `host_id` is explicit. Each machine needs its own identity; do not copy identity files between machines. Run only one continuous Collector per host ID. `host_name` changes the display name only.

## Windows setup

Run `make collector-windows` on a machine with Go and Make in a POSIX shell. Copy `build/windows/curio-collector.exe` to Windows and double-click it. The single EXE embeds the window script and uses Windows PowerShell and Windows Forms. No separate scripts, Go or Python are needed on the target machine.

- **Initialize** creates the configuration and opens Notepad. Existing files are preserved.
- **Edit configuration** opens the YAML used by the installed service in Notepad, or `%AppData%\curio\collector.yaml` when no service is installed. The path is read again on each operation.
- **Service** offers Install, Manage and Uninstall. Install also upgrades an existing service. These operations request administrator access. Manage opens Windows Services to start, stop or restart **Curio Collector**.

Running the EXE without arguments opens the window, including from a terminal. Use `curio-collector.exe check` to validate configuration and `curio-collector.exe run` for foreground collection. Stop with Ctrl+C and restart after editing configuration. The window, service installation, upgrade, recovery and removal have been tested on Windows 10. System reboot and herdr integration remain unverified.

## Source configuration

Each source has a `module`, an optional `interval` (default `2s`) and an optional `timeout` (inherits the global timeout). Durations use Go syntax such as `500ms` or `2s` and must be positive. Source IDs follow the same format as `host_id`.

`max_age` sets the source's data lifetime. It defaults to the greater of `10s` and three sampling intervals, rounded up to whole milliseconds; it must cover `interval`, use whole milliseconds, and not exceed seven days. Intervals whose default exceeds seven days require an explicit `max_age`. Collector sends this as `max_age_ms`; publishing cached data preserves its age. Host connectivity is tracked separately by the Hub.

Sources sample independently. The publisher sends their latest results and does not queue historical snapshots while disconnected. Removing a source or metric field withdraws it in the next snapshot.

For metric modules, omit `fields` to publish every field or use `fields: []` to publish no metric values. Unknown and duplicate field names, and options belonging to another module, are rejected. `herdr` does not accept metric fields.

CPU usage uses the difference between consecutive counters. The first sample has status `warming_up` and no CPU values, including logical count; `--once` and `--dry-run` therefore do not show CPU values. Continuous collection supplies them after the next valid sample. Memory `used_bytes` is total minus available memory, using the operating system's available-memory estimate.

### Disk and network

Add entries under `sources`, using paths and interface names from the machine running Collector:

```yaml
sources:
  storage:
    module: disk
    path: /
    interval: 30s
    fields: [usage_pct, free_bytes]
  lan:
    module: network
    interface: eth0
```

`disk.path` is required and selects the containing filesystem, not the size of a directory. Relative paths resolve from the YAML directory. Windows paths can use single quotes, for example `path: 'C:\'`. Capacity fields use bytes; `usage_pct` follows the operating system's usable-space accounting, so reserved blocks may affect the percentage.

`network.interface` is required and must match one interface name. Rates are bytes per second, calculated from counter differences over the actual sampling interval. The first sample, a counter reset, or the first sample after an interface returns has status `warming_up`. An unreadable or missing interface has a collection error. `--once` and `--dry-run` cannot produce network rates because they sample only once. No interfaces are summed automatically.

Use separate source IDs for additional filesystems or interfaces. The normal per-host source and metric limits still apply.

### herdr

Start herdr separately. Collector reads `agent.list` and does not start herdr.

| Setting | Behavior |
| --- | --- |
| `socket` | Defaults to `herdr/herdr.sock` in the user's configuration directory on Linux/macOS |
| `cli` | Set `true` to call the CLI instead of the socket; Windows uses the CLI by default |
| `session` | Selects a herdr session and forces CLI mode |
| `binary` | CLI executable; default `herdr` |
| `project` | `basename` (default) publishes the last project-directory component; `none` hides projects |
| `project_aliases` | Maps original project basenames to display names; filtering still uses the original basename |
| `allow_projects` | Allowed project basenames |
| `allow_agents` | Allowed agent brands |
| `allow_panes` | Allowed pane IDs |
| `allow_titles` | Default `false`; permits custom herdr titles in agent names |

Empty allow lists impose no restriction. Entries within one list are alternatives; different lists must all match. Check `--dry-run` before enabling custom titles, which can contain private information.

## Services

### Windows

Prepare YAML, the token and the certificate, then select **Service → Install** in the window. Installation validates the configuration, creates an identity if needed, copies the EXE to `%ProgramFiles%\Curio\curio-collector.exe`, and starts an automatic service named `curio-collector`. It runs at system boot without an interactive login. The service uses the same YAML file; it does not copy the configuration.

The default account is the low-privilege `LocalService`. Installation grants the service's configured account read access to the YAML, token, CA and identity files. It does not grant access to any other files. If file references or the service account change, install again to grant the required access. Service errors go to **Event Viewer → Windows Logs → Application**, source `curio-collector`.

LocalService has its own profile, PATH and login session. User-specific herdr installations, sessions and mapped drives are not inherited. For herdr, set an absolute `binary` in its source and, when needed, a `session`. Select the appropriate account in the service's **Log On** properties. Using the same account does not guarantee access to an interactive herdr session: check that its source reports `ok` in the Hub after restarting the service. `collector check` validates configuration, not herdr access; foreground collection uses the current user's environment. Windows herdr integration under a service account remains unverified.

SCM restarts the service after abnormal exits, including a reported failure: after 5 seconds for the first failure and 30 seconds for the second, then no further retries. The failure count resets after 60 seconds without another failure. A normal stop does not restart it. An unreachable Hub or failed source does not stop the process; Collector continues its normal retry loop.

The same operations are available in an administrator terminal:

```powershell
.\curio-collector.exe service install --config "$env:APPDATA\curio\collector.yaml"
.\curio-collector.exe service config
Restart-Service curio-collector
Stop-Service curio-collector
Start-Service curio-collector
.\curio-collector.exe service uninstall
```

On first installation, `service install` defaults to the current user's YAML path. On upgrade it keeps the registered path unless `--config` explicitly selects another file. The window reads the service configuration before requesting elevation, preserving that path even when another administrator account approves the request. `service config` prints the same path without administrator access; it returns the user's default only when no service is installed. Query errors are reported rather than silently selecting another file.

Restart the service after changing YAML. To upgrade, run Install from the new EXE. It validates configuration, stops the service, replaces its executable and starts it again. Configuration, credentials, identity, login account and startup type are preserved; a disabled service stays disabled. Installation applies the recovery policy above. Uninstallation stops and removes the service and its event source, preserving the EXE, YAML, credentials and identity. Only one Collector should report a given host ID; stop foreground collection before starting its service.

### Linux

Use Make from the repository root to install or remove a Linux systemd service. Python 3 is required; prepare YAML and credentials first. Installation preserves configuration, credentials and identity files.

```sh
make install-collector
make install-collector MODE=system
make uninstall-collector
```

Installation builds with Go 1.22+ (`GO=/path/to/go` selects the executable). `BINARY=/path/to/curio-collector` installs an existing executable without building. The installer checks configuration, copies the binary, writes the unit, enables and restarts it, then checks that it is active. Repeat the install command to upgrade.

| Mode | Binary | Unit | Startup |
| --- | --- | --- | --- |
| `MODE=user` (default) | `$HOME/.local/bin/curio-collector` | User configuration directory, `systemd/user/curio-collector.service` | User session |
| `MODE=system` | `/usr/local/bin/curio-collector` | `/etc/systemd/system/curio-collector.service` | System boot |

User mode requires an available user systemd manager and a non-root user. To run without logging in, an administrator must enable linger for that account.

System mode uses sudo to install but runs Collector as a non-root account. `RUN_AS=USER` selects that account; it defaults to the current user, and root must specify it. The service uses that account's home, configuration, credentials, identity and herdr. It does not start herdr; collection retries when herdr becomes available.

`CONFIG=/path/to/collector.yaml` selects YAML. The default is `curio/collector.yaml` in the runtime user's configuration directory. Installation for the current user respects `XDG_CONFIG_HOME`; selecting another account defaults to that account's `$HOME/.config`. Paths are determined before sudo and configuration is checked as the runtime account.

```sh
make install-collector MODE=system CONFIG="$HOME/.config/curio/collector.yaml"
make install-collector BINARY=/path/to/curio-collector CONFIG=/path/to/collector.yaml
make uninstall-collector MODE=system
```

Choose one service mode per host ID. Installation rejects a known unit for the other mode or drop-in overrides beside the target unit. Review or remove old overrides and uninstall the old mode before switching. The [unit template](../scripts/collector/curio-collector.service) is available for manual deployment; the installer writes absolute executable and configuration paths.

```sh
systemctl --user status curio-collector.service
journalctl --user -u curio-collector.service -n 30 --no-pager
"$HOME/.local/bin/curio-collector" check
systemctl --user restart curio-collector.service
```

For a system service, use `sudo systemctl` and `sudo journalctl` without `--user`. After editing YAML, run Collector `check --config PATH` as the runtime account before restarting. Uninstallation removes the service but preserves the executable, YAML, credentials and identity files.

On macOS, run Collector in the foreground or configure launchd manually. Building directly with `go build` in `collector/` does not require Make.
