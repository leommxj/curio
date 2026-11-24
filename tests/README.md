# Host tests

Run commands from the repository root. `make test` runs Go, C, deployment/workflow and integration tests; `make test-ui` and `make test-web` run suites with extra dependencies. C tests need a C compiler; most also need cJSON, and configuration tests need libyaml. UI tests require the full LVGL 9 source tree, defaulting to `firmware/managed_components/lvgl__lvgl`. Web tests need Playwright and Chromium; integration tests also need Python 3 and Go. Set `GO` to select a Go executable and `PYTHON` to select a Python environment. For example, `make test-web PYTHON=/path/to/venv/bin/python`.

```sh
make deps-host
python3 -m pip install playwright
python3 -m playwright install chromium
```

| Suite | Command | Coverage |
| --- | --- | --- |
| Core | `make -C tests/core test` | Snapshot validation, status retention, independent host/source health, numeric formatting and allocation failures |
| Layout | `make -C tests/layout test` | Grid placement, size, overlap, bindings and pixel bounds |
| Clock | `make -C tests/clock test` | UTC offsets, date boundaries, unsynchronized time and SNTP lifecycle |
| Feedback | `make -C tests/feedback test` | Independent outputs, temporary mute, queues, strength, shutdown and failures |
| Alerts | `make -C tests/alerts test` | Rule validation, baselines, thresholds, offline duration, queues and allocation failures |
| Battery | `make -C tests/battery sanitize` | I²C failures, signed current, charging, absent batteries and stale readings |
| Orientation | `make -C tests/orientation sanitize` | Four directions, stability, motion, IMU lifecycle and sensor scheduling |
| Config | `make -C tests/config sanitize` | YAML defaults, canonical round trips, strict validation, duplicate keys and allocation failures |
| Web | `python3 -m unittest discover -s tests/web -v` | Widget editing, status cards, numeric formatting, undo, form/YAML consistency, import/export and tokens |
| Settings | `make -C tests/settings sanitize` | Dual-slot YAML, ETags, token changes, NVS failures and reboot recovery |
| Provisioning | `make -C tests/provision test` | Iris chunk ordering, setup access, failed writes, TLS errors and restart |
| UI | `make -C tests/ui test` | Real LVGL input, clock sizing and updates, paging, Settings, alert details, temporary mute and popups |
| Integration | `python3 -W error::ResourceWarning tests/integration/test_collector_hub.py` | Real Collector and development Hub authentication, persistence, capacity and source updates |
| Scripts | `python3 -m unittest discover -s tests/scripts -v` | Make build/flash ordering, deployment arguments, generated units, sudo paths and uninstall behavior |
| Collector | `cd collector && go test ./...` | Configuration, source lifetimes, herdr mapping, HTTPS, disk sampling, CPU/network counter differences and scheduling |

Core, layout, alerts and feedback also support `sanitize` for AddressSanitizer, UndefinedBehaviorSanitizer and leak checks. Host C tests do not require ESP-IDF. Core and alerts accept `CJSON_DIR` to override `.deps/cJSON`.

Deployment tests use temporary directories and command substitutes; they do not install or change real services. Generated units are checked with systemd, but permissions, startup and restart still need verification on the target machine. Run `go test -race ./...` and `go vet ./...` in `collector/` for Go concurrency and static checks.

On Windows, `go test ./...` in `collector/` also checks service startup validation, status queries, stop/shutdown, cancellation of an active HTTP request, registered configuration paths, built-in account SIDs and executable replacement. `pwsh -NoProfile -File tests/scripts/test_windows_window.ps1` checks configuration lookup, editor arguments and UAC command construction without elevation or service changes; Windows PowerShell can run it with `powershell` instead. These checks do not verify SCM installation and upgrade, account permissions, automatic startup, recovery actions or the visible Windows GUI.

Override LVGL with `make -C tests/ui test LVGL_DIR=/path/to/lvgl`. UI tests compile the real UI, core, layout and LVGL, with stubs for board support, FreeRTOS, audio and health services. C test artifacts go to `build/<suite>-tests/`. Host tests cannot verify physical touch, LCD output, panel orientation, actual sound/vibration/flash, sensor hardware or ESP32 resource limits.

Integration tests compile the Collector and start `scripts/dev/hub.py` on a random loopback port. The Hub uses the firmware's C core and YAML model via `build/libcurio_dev.so`. Tests manage temporary configuration, credentials and Collector binaries. They cover status snapshots, status widget YAML and independent data lifetimes. On Linux they also exercise native CPU, memory, disk and network sampling and verify that a failed herdr source does not stop other sources. Native Windows/macOS sampling requires testing on those platforms.

## Local development Hub

The Hub serves the firmware web interface and C models over loopback HTTP:

```sh
mkdir -p build
python3 -c "from pathlib import Path; import secrets; [Path('build/' + name).write_text(secrets.token_hex(32)) for name in ('dev-token', 'dev-admin-token')]"
make dev-hub ARGS="--token-file build/dev-token --admin-token-file build/dev-admin-token"
```

Open `http://127.0.0.1:7340` and log in with `build/dev-admin-token`. For a test Collector, use that endpoint, `allow_http_loopback: true`, and a token path relative to its YAML file or an absolute path to `build/dev-token`; omit `ca`.

The Hub binds only to loopback. It saves full configuration, including current tokens, in `build/dev-state/curio.yaml`. Token files initialize new configurations only. It does not emulate the board's networking, NVS or display drivers.
