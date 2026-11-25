# Host tests

Run commands from the repository root. `make test` runs Go tests with the race detector, `go vet`, C, deployment/workflow and integration tests; `make test-ui` and `make test-web` run suites with extra dependencies. C tests need a C compiler; most also need cJSON, and configuration tests need libyaml. Web tests need Playwright and Chromium; integration tests also need Python 3 and Go. Set `GO` to select a Go executable and `PYTHON` to select a Python environment. For example, `make test-web PYTHON=/path/to/venv/bin/python`.

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
| Weather | `make -C tests/weather sanitize` | Shared location caches, refresh/retry timing, invalid responses, stale values and removal |
| Wi-Fi | `make -C tests/wifi test` | Network order, failover, reconnect, DHCP timeouts and driver failures |
| Feedback | `make -C tests/feedback test` | Independent outputs, temporary mute, queues, strength, shutdown and failures |
| Alerts | `make -C tests/alerts test` | Rule validation, baselines, thresholds, offline duration, queues and allocation failures |
| Battery | `make -C tests/battery sanitize` | I²C failures, signed current, charging, absent batteries and stale readings |
| Orientation | `make -C tests/orientation sanitize` | Four directions, stability, motion, IMU lifecycle and sensor scheduling |
| Config | `make -C tests/config sanitize` | YAML defaults, canonical round trips, strict validation, duplicate keys and allocation failures |
| Web | `python3 -m unittest discover -s tests/web -v` | Widget editing, city search, Wi-Fi setup/reset, form/YAML consistency, import/export and tokens |
| Settings | `make -C tests/settings sanitize` | Dual-slot YAML, ETags, token changes, reset, NVS failures and reboot recovery |
| Provisioning | `make -C tests/provision test` | Iris chunk ordering, setup access, failed writes, TLS errors and restart |
| UI | `make -C tests/ui test` | Real LVGL input, Chinese rendering, clocks, weather, paging, Settings and alerts |
| Integration | `python3 -W error::ResourceWarning tests/integration/test_collector_hub.py` | Real Collector and development Hub authentication, persistence, capacity and source updates |
| Scripts | `python3 -m unittest discover -s tests/scripts -v` | Make build/flash ordering, deployment arguments, generated units, sudo paths and uninstall behavior |
| Collector | `cd collector && go test ./...` | Configuration, source lifetimes, herdr mapping, HTTPS, disk sampling, CPU/network counter differences and scheduling |

Module tests live beside their implementations as `firmware/modules/widgets/<type>/test.c` and `firmware/modules/alerts/<type>/test.c`. The registry generator adds them to the layout and alert suites. Each module's `cases.json` checks C/browser configuration parity in the browser suite. Cross-module configuration, UI and browser scenarios remain in `tests/`.

Core, layout, alerts and feedback also support `sanitize` for AddressSanitizer, UndefinedBehaviorSanitizer and leak checks. Host C tests do not require ESP-IDF. Core and alerts accept `CJSON_DIR` to override `.deps/cJSON`.

Deployment tests use temporary directories and command substitutes. UI tests compile LVGL and FreeType with board and FreeRTOS stubs, and render the embedded Chinese font. They need CMake, LVGL 9.5 and FreeType 2.15 sources; the default paths use ESP-IDF's `firmware/managed_components/` populated during firmware configuration. Override with `make test-ui LVGL_DIR=/path/to/lvgl FREETYPE_DIR=/path/to/freetype`. Hardware, service installation and GUI behavior require checks on their target platforms.

On Windows, run `go test ./...` in `collector/` for service lifecycle tests and `powershell -NoProfile -File tests/scripts/test_windows_window.ps1` for setup-window command tests.

Integration tests build the Collector and start the development Hub on a random loopback port with temporary configuration. On Linux they also exercise native system metrics and source-failure isolation.

## Local development Hub

The Hub serves the firmware web interface and C models over loopback HTTP:

```sh
mkdir -p build
python3 -c "from pathlib import Path; import secrets; [Path('build/' + name).write_text(secrets.token_hex(32)) for name in ('dev-token', 'dev-admin-token')]"
make dev-hub ARGS="--token-file build/dev-token --admin-token-file build/dev-admin-token"
```

Open `http://127.0.0.1:7340` and log in with `build/dev-admin-token`. For a test Collector, use that endpoint, `allow_http_loopback: true`, and a token path relative to its YAML file or an absolute path to `build/dev-token`; omit `ca`.

The Hub binds only to loopback. It saves full configuration, including current tokens, in `build/dev-state/curio.yaml`. Token files initialize new configurations only.
