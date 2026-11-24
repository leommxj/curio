# Firmware

Curio targets ESP32-S31 with the Mosaico BSP and LVGL 9. It builds with ESP-IDF and installs through the official Vibe Mode recovery layout.

The [partition table](../firmware/partitions.csv) reserves 12.9375 MiB for `main_app` (`app/ota_0`) and 1.75 MiB for `vibe_mode`. The `curio_check_size` build target and System Update packaging check the application against `main_app`.

## Build

Install Make, Python 3.12+, curl, and the ESP-IDF revision in [dependencies.json](../dependencies.json), including its ESP32-S31 toolchain and Python environment. Activate ESP-IDF, then run from the repository root:

```sh
. /path/to/esp-idf/export.sh
make firmware
```

`make firmware` fetches pinned dependencies into `.deps/`, checks the ESP-IDF revision, prepares the official Iris Python environment for packaging, then builds the application and update bundle. Output defaults to `build/firmware/curio-system-update.irisfw`; override `FIRMWARE_BUILD_DIR` to use another build directory. `make deps` downloads source dependencies without preparing the Python environment or building. Firmware images do not contain Wi-Fi credentials, tokens or TLS keys.

## First setup

Install the firmware with `make flash`.

On first initialization, the device tries `wifi_ssid` and `wifi_password` from Vibe Mode's **Settings → Shared information** (`sysmeta/shared`). A successful connection is copied into Curio's YAML with generated TLS credentials. Missing or unusable shared settings open the Setup AP. Curio never writes to Vibe Mode's settings. After automatic connection, hold the AI button for three seconds to open setup and download the credentials, or provision from a computer while that AP is open.

1. Join the `Curio-…` Wi-Fi network using the random password shown on the board. Open `http://192.168.4.1/setup.html`, enter the network SSID, password and hostname, then save. An empty Wi-Fi password selects an open network. Download the certificate and full YAML, then select **Connect** to restart the device. The setup page requires no separate login because its temporary AP has the displayed WPA2 password. Enter the address manually.
2. For setup from a computer, install OpenSSL, run `make configure SSID="YourWiFi"` and then `make provision`. The latter writes the YAML over Iris, by USB or paired Wi-Fi, and restarts the device. Provisioning is accepted only while the device is unconfigured or its physical Setup AP is open. An ordinary network error never opens that AP. The configuration tool prompts for the Wi-Fi password; `ARGS="--password-env VARIABLE"` reads it from an environment variable, and `ARGS="--open-wifi"` selects an open network. Use `ARGS="--ip 192.168.1.50"` to include a device IP address in the certificate SAN. The generated YAML has one `network.wifi` entry; edit it to add up to four networks in connection order before provisioning.

`make configure` creates `firmware/local/` and refuses to overwrite an existing directory. Its files are:

| File | Purpose |
| --- | --- |
| `curio.yaml` | Complete Hub configuration for `make provision`, including Wi-Fi, TLS and tokens |
| `server-cert.pem` | Trust certificate for Collectors and browsers |
| `token.txt` | Collector token |
| `admin-token.txt` | Web administration token |
| `server-key.pem` | TLS private key |

The computer-generated certificate is valid for 3650 days. When setup is performed on the device without TLS fields, the device generates a certificate valid from 2024 through 2049. Save the downloaded certificate for browsers and Collectors. Full YAML exports contain the Wi-Fi password, both tokens, certificate and private key in plain text. Network, hostname and certificate changes are stored in YAML and take effect after a restart.

## Install and update

`make flash` builds the current source and installs its bundle with the official Mosaico System Update tool. It works over USB or paired Wi-Fi:

```sh
make doctor
make flash
make status
```

Normal `make firmware` and `make flash` do not replace the application's saved NVS configuration. `make provision` explicitly writes a new configuration under the setup gate described above.

For an existing package, use `make flash BUNDLE=/path/to/curio-system-update.irisfw`; this skips building. Set `DEVICE=DEVICE_ID` when multiple boards are connected. The official tools validate the application, partition identity and boot health. `.mosaico.json` supplies their workspace configuration; `scripts/firmware/mosaico.py` is the internal adapter.

Network updates require host pairing and Wi-Fi in both Curio and Vibe Mode. Their Wi-Fi settings are separate. Pair once over USB with the official Iris tools and configure Vibe Mode's Wi-Fi before updating over the network. The update enters Vibe Mode, transfers the firmware over its paired control/data links (ports 19772/19773), then returns to Curio. USB is needed for initial pairing or recovery when network access is unavailable; keep it connected through reboots when using it.

For a blank board or incompatible factory layout, back up Flash, enter ROM Download Mode, then install the official base firmware:

```sh
make recover MAC=DEVICE_MAC
```

Recovery replaces the bootloader, partition table and some factory applications. Confirm that Vibe Mode starts before updating Curio. See the pinned official [application integration guide](https://github.com/esp-mosaico/esp-mosaico-utils/blob/f58f4f9e5363f908b17a8e1ea8fdf4dd77561ef1/mosaico-tools/docs/application-integration.md).

## Connection and troubleshooting

The default mDNS name is `curio.local`. If mDNS is unavailable, use the short hostname through local DNS or an IP address included in the certificate SAN. Configure certificate trust separately for Collectors and browsers. The web **Device** tab can add, remove and reorder up to four Wi-Fi networks, change the hostname and download the certificate. Save and restart to apply network changes. Changing the hostname generates a new certificate; custom TLS values can be imported through the full YAML editor.

At startup, Curio tries networks in list order, allowing up to fifteen seconds per attempt and two seconds between attempts. After losing a connection it retries that network, then continues through the list. It stays on a working network and keeps retrying if all networks fail.

While Curio is running, hold the AI button (GPIO7) for three seconds or tap **Settings → Wi-Fi** to start the Setup AP. Join the `Curio-…` network using the eight-digit password shown on the screen, then open `http://192.168.4.1`. Edit or add networks, save, and select **Connect** to restart with the saved settings. Opening setup preserves existing configuration and briefly reconnects the current Wi-Fi connection; **Close** stops the AP without restarting. Each AP session gets a new password. The BOOT button keeps its official recovery function.

The setup page's **Reset Curio** button asks for confirmation, then restarts with default settings and new tokens. It clears saved Wi-Fi, certificates, layouts and alerts from both configuration slots. Firmware and Vibe Mode settings remain intact. After reset, the device opens its Setup AP without importing shared Wi-Fi; reconnect using the new password on the screen.

`MOSAICO_UTILS_ROOT=/path/to/esp-mosaico-utils` selects an alternate checkout for the device-tool adapter; use the pinned revision.

## Runtime and controls

Portable components separate data state (`curio_core`), YAML (`curio_config`), widget geometry and bindings (`curio_layout`), and alert rules (`curio_alerts`). Device integration lives in `main`: `curio_settings` owns persistent configuration, `curio_network` owns HTTPS and network time, and `curio_ui` renders the current page. Sensors and feedback use their own tasks. The development Hub reuses the same C models through `scripts/dev/bridge.c`.

Widget and alert implementations live in `firmware/modules/widgets/<type>/` and `firmware/modules/alerts/<type>/`. Each directory owns its configuration, runtime, web interface and tests. `firmware/modules/registry.json` is the build registry; see [modules.md](modules.md) for interfaces and extension steps.

Type-specific options are allocated only for configured entries; only the active page creates LVGL views. Disabled rules retain their configuration but have no detection state. With no enabled rules, the application creates no alert runtime.

The display starts before networking. Wi-Fi reconnects after disconnection; existing data becomes stale or offline according to its age. Network parsing and validation run outside the application lock, with state committed under a short lock. Request bodies, parsed objects and source storage use PSRAM. Widgets share the UI task and are destroyed when changing pages.

`curio_fonts` shares the Mosaico factory font across widgets and alerts. FreeType renders Chinese on demand with a bounded glyph cache; the embedded font and cached bitmaps use PSRAM. See [fonts](../firmware/components/curio_fonts/README.md) for coverage and licensing. Protocol text limits remain UTF-8 byte limits.

Clock widgets use the system clock, synchronized by ESP-IDF SNTP while any page contains a clock or weather widget. The existing network task manages synchronization. NTP server changes and Wi-Fi reconnection restart synchronization. Removing all widgets that need time stops it. The device keeps time during network outages; before initial synchronization clocks show `--:--`. Set the NTP server in the web Device tab and each clock's fixed UTC offset in its widget settings. Daylight-saving changes are manual.

Weather uses Open-Meteo over HTTPS after time synchronization. The network task advances one asynchronous request at a time; views only read cached values. A response uses a temporary 2 KiB buffer in PSRAM. Configured locations share caches by coordinates; removing a location frees its cache, and removing all weather widgets cancels any request. City search runs in the browser during configuration.

Device, screen, alert, access, network and TLS settings use two YAML slots in application NVS. Boot selects the newer valid configuration. Snapshots, temporary filters, page changes and queued notifications do not write Flash. See the [protocol](protocol.md) for fields and limits.

Tap the page title at the top left to cycle through configured pages. Swiping an Agent list pages through that list; it does not switch screen pages.

The top-right gear opens Settings: automatic rotation, automatic paging for Agent lists on the current page, brightness, volume, vibration strength, the global flash switch and Wi-Fi setup. Differing list settings show `Mixed`; toggling applies to all lists on the page. Paging pauses while Settings is open. Switches save immediately; sliders save on release, with live brightness preview. Device brightness ranges from 5–100%; the web interface allows 0–100%, where zero can blank the display. Volume and vibration range from 0–100%, with zero disabling that output.

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
