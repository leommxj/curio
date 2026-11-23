# Firmware

Curio targets ESP32-S31 with the Mosaico BSP and LVGL 9. It builds with ESP-IDF and installs through the official Vibe Mode recovery layout.

The [partition table](../firmware/partitions.csv) reserves 12.9375 MiB for `main_app` (`app/ota_0`) and 1.75 MiB for `vibe_mode`. The `curio_check_size` build target and System Update packaging check the application against `main_app`.

## Build

Install Make, Python 3.12+, curl, OpenSSL, and the ESP-IDF revision in [dependencies.json](../dependencies.json), including its ESP32-S31 toolchain and Python environment. Activate ESP-IDF, then run from the repository root:

```sh
. /path/to/esp-idf/export.sh
make configure SSID="YourWiFi"
make firmware
```

`make firmware` fetches pinned dependencies into `.deps/`, checks the ESP-IDF revision, then invokes ESP-IDF to build the application and official update bundle. Output defaults to `build/firmware/curio-system-update.irisfw`; override `FIRMWARE_BUILD_DIR` to use another build directory. `make deps` downloads dependencies without building.

The configuration tool prompts for the Wi-Fi password; `--password-env VARIABLE` reads it from an environment variable, and `--open-wifi` selects an open network. Pass extra options through `ARGS`, for example `make configure SSID="YourWiFi" ARGS="--ip 192.168.1.50"`, to include an IP address in the certificate SAN. Output defaults to `firmware/local/`; an existing directory is never overwritten.

| File | Purpose |
| --- | --- |
| `curio_config.h`, `curio_tls.h` | Compiled Wi-Fi settings, initial tokens, certificate and private key |
| `server-cert.pem` | Trust certificate for Collectors and browsers |
| `token.txt` | Initial Collector token |
| `admin-token.txt` | Initial web administration token |
| `server-key.pem` | TLS private key; keep on the build machine |
| `deployment.json` | Endpoint and certificate metadata |

The certificate expires after 365 days. Regenerate deployment inputs and rebuild before expiry. Firmware images contain credentials. Without local configuration, Wi-Fi and HTTPS stay disabled; the Wi-Fi status popup shows `Setup required`.

The compiled tokens initialize `access` only when no valid YAML configuration exists. Token changes through the web interface persist immediately; full YAML exports include both tokens in plain text. Changes to Wi-Fi, hostname or TLS inputs require rebuilding.

## Install and update

`make flash` builds the current source and installs its bundle with the official Mosaico System Update tool. It works over USB or paired Wi-Fi:

```sh
make doctor
make flash
make status
```

For an existing package, use `make flash BUNDLE=/path/to/curio-system-update.irisfw`; this skips building. Set `DEVICE=DEVICE_ID` when multiple boards are connected. The official tools validate the application, partition identity and boot health. `.mosaico.json` supplies their workspace configuration; `scripts/firmware/mosaico.py` is the internal adapter.

Network updates require host pairing and Wi-Fi in both Curio and Vibe Mode. Their Wi-Fi settings are separate. Pair once over USB with the official Iris tools and configure Vibe Mode's Wi-Fi before updating over the network. The update enters Vibe Mode, transfers the firmware over its paired control/data links (ports 19772/19773), then returns to Curio. USB is needed for initial pairing or recovery when network access is unavailable; keep it connected through reboots when using it.

For a blank board or incompatible factory layout, back up Flash, enter ROM Download Mode, then install the official base firmware:

```sh
make recover MAC=DEVICE_MAC
```

Recovery replaces the bootloader, partition table and some factory applications. Confirm that Vibe Mode starts before updating Curio. See the pinned official [application integration guide](https://github.com/esp-mosaico/esp-mosaico-utils/blob/f58f4f9e5363f908b17a8e1ea8fdf4dd77561ef1/mosaico-tools/docs/application-integration.md).

## Connection and troubleshooting

The default mDNS name is `curio.local`. If mDNS is unavailable, use the short hostname through local DNS or an IP address included in the certificate SAN. Configure certificate trust separately for Collectors and browsers.

`MOSAICO_UTILS_ROOT=/path/to/esp-mosaico-utils` selects an alternate checkout for the device-tool adapter; use the pinned revision.

## Runtime and controls

Portable components separate data state (`curio_core`), YAML (`curio_config`), widget geometry and bindings (`curio_layout`), and alert rules (`curio_alerts`). Device integration lives in `main`: `curio_settings` owns persistent configuration, `curio_network` owns HTTPS and network time, and `curio_ui` renders the current page. Sensors and feedback use their own tasks. The development Hub reuses the same C models through `scripts/dev/bridge.c`.

The display starts before networking. Wi-Fi reconnects after disconnection; existing data becomes stale or offline according to its age. Network parsing and validation run outside the application lock, with state committed under a short lock. Request bodies, parsed objects and source storage use PSRAM. Widgets share the UI task and are destroyed when changing pages.

Clock widgets use the system clock, synchronized by ESP-IDF SNTP while any page contains a clock. The existing network task manages synchronization; no clock task is created. NTP server changes and Wi-Fi reconnection restart synchronization. Removing all clocks stops it. The device keeps time during network outages; before initial synchronization it shows `--:--`. Set the NTP server in the web Device tab and each clock's fixed UTC offset in its widget settings. Daylight-saving changes are manual.

Device, screen, alert and access settings use two YAML slots in application NVS. Boot selects the newer valid configuration. Snapshots, temporary filters, page changes and queued notifications do not write Flash. See the [protocol](protocol.md) for fields and limits.

The top-right gear opens Settings: automatic rotation, automatic paging for Agent lists on the current page, brightness, volume, vibration strength and the global flash switch. Differing list settings show `Mixed`; toggling applies to all lists on the page. Paging pauses while Settings is open. Switches save immediately; sliders save on release, with live brightness preview. Device brightness ranges from 5–100%; the web interface allows 0–100%, where zero can blank the display. Volume and vibration range from 0–100%, with zero disabling that output.

`Alerts` shows scrollable rule details; edit rules in the web interface. `Test alert` displays a ten-second popup using current output settings, including flash when globally enabled. A real notification replaces the test. The top sound and vibration buttons temporarily mute their respective outputs until reboot, including tests, without changing YAML. Popups and flash remain available. Tap the Wi-Fi icon to show status and IP for five seconds.

Device and web edits share YAML and ETags. After a device edit, saving from a web page with an old ETag returns 409 and requires reloading.

## Hardware behavior

Alert state, Agent tracking and popup/Settings interfaces are allocated as needed. Sound, vibration and flash share an on-demand feedback task and queue that remain allocated after first use. Audio initializes on first playback; vibration alone does not initialize it. Vibration lasts about 240 ms and uses BSP motor PWM for strength.

The official camera module's flash uses active-low, open-drain GPIO34. Each output flashes twice for 80 ms, separated by 80 ms. GPIO initializes on first use without starting the camera. Real alerts require both global and rule flash switches; tests require only the global switch. Diagnostic counters report driver calls, not physical light output.

`curio_battery` reads standard BQ27220 registers over the BSP's shared I²C bus without changing calibration or capacity settings. The sensor task samples every five seconds outside the UI lock. Failed readings, or readings older than fifteen seconds, display `--`. Accuracy depends on existing battery calibration. Positive battery current indicates charging; USB connection alone does not.

Automatic rotation configures the BMI270 accelerometer at 25 Hz and reads it every 100 ms in the sensor task, with the gyroscope disabled. A stable orientation for 500 ms rotates display and touch through the BSP. Flat placement, boundary positions, motion and read failures retain the current direction. Disabling automatic rotation releases the IMU and saves the current direction. Rotation runs outside the LVGL and application locks and does not write Flash on each rotation.

The first completed frame flush confirms Iris boot health. Official components manage USB and paired Iris TCP channels; application code does not modify recovery partitions or `sysmeta`.

```sh
make screenshot OUTPUT=/path/to/device.png
```

Screenshots redraw the active LVGL screen using a temporary 480×480 RGB565 buffer (about 450 KiB), released after transfer. They do not read the physical LCD; verify panel orientation and touch on the device.
