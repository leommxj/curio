# State and configuration protocol

The Hub accepts v2 snapshots from one Collector per host. Collectors read their own configuration; the Hub does not distribute collection policies.

Snapshots carry numeric metrics (`kind: metrics`), a status (`kind: status`), or agent lists (`kind: agents`). Collection modules and operating systems are local implementation choices and are not part of the protocol. Custom reporters use the same API and authentication as the [built-in modules](collector.md#supported-modules-and-platforms).

## Transport and authentication

Use HTTPS on port 443 and `Authorization: Bearer <token>`. JSON and YAML use UTF-8. Collector skips certificate verification by default; `verify_tls: true`, `--verify-tls` or a configured `ca` enables it. Verification requires a trusted certificate whose SAN covers the connection hostname or IP.

| Credential | Access |
| --- | --- |
| `access.collector_token` | `POST /api/v2/snapshot`, for any host ID |
| `access.admin_token` | State, catalog, configuration, token management, diagnostics |

Static web assets and `/healthz` are public on the normal HTTPS server. The temporary WPA2 Setup AP serves `http://192.168.4.1/setup.html` and the setup API without a separate token; access uses the random Wi-Fi password shown on the device. See [first setup](firmware.md#first-setup) for shared Wi-Fi import and [device controls](firmware.md#connection-and-troubleshooting) for opening the AP. Device credentials live in saved YAML in NVS, not in the firmware image. Full YAML exports contain Wi-Fi passwords, both tokens and the TLS private key in plaintext; keep them out of Git.

## Snapshots

`POST /api/v2/snapshot` accepts at most 16,384 bytes:

```json
{
  "v": 2,
  "host_id": "workstation",
  "host_name": "Workstation",
  "sources": {
    "system.cpu": {
      "kind": "metrics",
      "status": "ok",
      "sample_age_ms": 120,
      "metrics": {
        "usage_pct": {"unit": "percent", "value": 32.4},
        "logical_count": {"unit": "count", "value": 8}
      }
    },
    "herdr": {
      "kind": "agents",
      "status": "ok",
      "sample_age_ms": 20,
      "agents": [
        {"id": "terminal-123", "name": "codex", "brand": "codex", "project": "curio", "state": "working"}
      ]
    }
  }
}
```

Limits apply to each complete host snapshot: 8 sources, 32 metrics, and 16 agents. The Hub holds at most 4 hosts. Excess entries are rejected, never truncated.

- Host IDs, source IDs, and metric keys contain 1–48 ASCII bytes from `[A-Za-z0-9._-]`. Host names contain 1–48 UTF-8 bytes and do not affect matching.
- Source `kind` is `metrics`, `status`, or `agents`. Outer `status` describes collection: `ok`, `error`, `unavailable`, or `warming_up`. Required `sample_age_ms` is the sample's age at transmission, as a nonnegative JSON safe integer. Optional `max_age_ms` is a positive integer up to 604,800,000 (seven days), default 10,000; it controls data expiry and appears in state and catalog responses.
- Metric units contain 1–16 printable ASCII bytes without whitespace. `percent` values must be between 0 and 100. `bytes` and `count` values must be nonnegative safe integers. `bytes_per_second` accepts nonnegative finite numbers. Other units accept finite numbers. The safe integer maximum is 9,007,199,254,740,991.
- Successful metric sources require every metric's `value`. Failed sources still declare metric keys and units but must omit values. Zero does not represent missing data.
- Agents require `id` (1–96 UTF-8 bytes), `name` (1–64), `project` (0–48), and `state` (`blocked`, `working`, `done`, `idle`, or `unknown`). Optional `brand` contains 0–32 UTF-8 bytes. IDs must be unique within a source. Failed agent sources send an empty list.
- Successful status sources require a `state` object: `text` (1–64 UTF-8 bytes), optional `detail` (0–160 bytes), and optional `level` (`neutral`, `ok`, `warning`, or `error`, default `neutral`). Failed status sources must omit `state`. A business failure uses outer `status: ok` and `state.level: error`; failure to collect uses outer `status: error`.
- Strings must be valid UTF-8 without NUL. JSON nesting is limited to 32 levels. Duplicate object keys are rejected; unknown fields are ignored.

`sources` and each `metrics` object are complete inventories: omitted entries are removed. Empty sources and successful empty agent lists are valid. Failed metric sources retain previous values only for declared keys with unchanged units, preserving their age. Failed agent and status sources retain their previous successful data and age when the source kind is unchanged. Sending cached samples does not refresh their age.

A status source inside `sources` can be reported as:

```json
"service.api": {
  "kind": "status",
  "status": "ok",
  "sample_age_ms": 0,
  "max_age_ms": 60000,
  "state": {"text": "Online", "level": "ok", "detail": "HTTP 200"}
}
```

Invalid snapshots leave state unchanged. Success returns `200 {"ok":true}` without configuration instructions. Errors: 400 for invalid content or entry counts, 401 for authentication, 413 for body size, and 503 for host capacity or allocation failure.

Collector configuration and source selection are described in [collector.md](collector.md) and [the example](../collector/config.example.yaml).

## State and health

| Endpoint | Response |
| --- | --- |
| `GET /api/v2/catalog` | Reported hosts, sources, metric keys, units, data lifetimes and health; no metric values, status text or agent lists |
| `GET /api/v2/state` | Current and retained metric values with `age_ms`, status objects, source health and agent lists |
| `GET /api/v2/diagnostics` | Memory, tasks, widget instances, battery, orientation, and notification/output counters |
| `GET /healthz` | Version and runtime status; no authentication required |

State and catalog use `{"v":2,"hosts":[...]}`. Hosts contain `host_id`, `host_name`, `health`, and a `sources` object keyed by source ID. Metrics are objects keyed by metric key.

The receiver uses a monotonic clock. A host becomes stale after 10 seconds without a valid report and offline after 30 seconds. Source health is independent: samples older than their `max_age_ms` are stale, and collection failures are immediately unavailable. Updates from other sources do not refresh old samples. Host health measures reporting activity; source status and health describe collection and data freshness. Widgets show `Offline` when the host is offline, even if retained source data has not expired. Agent and metric alerts require fresh source data and a host that is not offline.

State lives in RAM and is rebuilt after reboot. Hosts offline for more than 5 minutes can be evicted when a new host arrives. Configuration references do not create catalog entries.

`diagnostics.battery` describes the built-in battery, independently of Collector data. Valid readings contain `status`, `level_pct`, `voltage_mv`, `current_ma`, `average_current_ma`, `remaining_capacity_mah`, `full_charge_capacity_mah`, `design_capacity_mah`, and `sample_age_ms`. Positive current charges the battery. Above 2 mA means `charging`; below -2 mA means `discharging`; near zero uses the gauge's full flag to select `full` or `idle`. Sampling runs every 5 seconds. Failed readings or readings older than 15 seconds are `unavailable`; an undetected battery is `absent`. Both return only `status` and `level_pct:null`.

## YAML configuration API

`GET /api/v2/config` returns `application/yaml; charset=utf-8` and an `ETag`. Send the unchanged ETag in `If-Match` with `PUT /api/v2/config` and `Content-Type: application/yaml`. Treat ETags as opaque. Missing or malformed preconditions return 428; a changed configuration returns 409. Success returns normalized YAML and the current ETag. Saving identical configuration does not write Flash.

Bodies are limited to 16 KiB and one mapping document. Unknown fields, duplicate keys, wrong types, multiple documents, anchors, aliases, and explicit tags are rejected. Use `true` and `false` for booleans. Invalid configuration returns 400, oversized bodies return 413, and storage failures return 503. Failures preserve the current configuration.

```yaml
access:
  admin_token: example-admin-token-0001
  collector_token: example-collector-token-0001
network:
  hostname: curio
  wifi:
    - ssid: Office Wi-Fi
      password: example-password
      ipv4:
        address: 192.168.1.50
        netmask: 255.255.255.0
        gateway: 192.168.1.1
        dns: 192.168.1.1
    - ssid: Guest Wi-Fi
screen:
  pages:
    - id: agents
      widgets:
        - type: agents
          at: [0, 0]
          size: [4, 6]
```

`access` and `screen.pages` are required. `version` defaults to 1 and only accepts 1. `device`, `alerts`, `network` and `tls` are optional. Tokens contain 16–128 printable ASCII bytes without whitespace. Importing a document updates its credentials, Wi-Fi and TLS settings. `network.wifi` is an ordered list of up to four networks. Each entry needs a unique SSID of 1–32 UTF-8 bytes; its password may be empty, 8–63 UTF-8 bytes, or a 64-digit hexadecimal WPA PSK. Omit `wifi` or use `wifi: []` for an unconfigured device. The device tries networks in list order. `network.hostname` defaults to `curio` and is one DNS label of at most 32 ASCII letters, digits and interior hyphens. `tls.certificate` and `tls.private_key` are PEM strings supplied together. When Wi-Fi is configured and TLS is absent, the device generates a certificate and key. Changing the hostname through the Device tab removes the old TLS pair so the device generates one for the new name. Network and certificate changes take effect after restart.

Import replaces the complete configuration; omitted optional fields return to their defaults. Export the current YAML before editing a configured device. The example above omits TLS so the device generates it; later exports include both PEM fields. On first initialization, Curio tries Vibe Mode's shared Wi-Fi settings as described in [first setup](firmware.md#first-setup).

Each Wi-Fi entry uses DHCP unless it has an `ipv4` mapping with all four fields shown above. Values must be dotted decimal IPv4 strings; `netmask` must be contiguous (`/1`–`/30`). Address and gateway must be distinct usable host addresses in the same subnet. DNS may be outside that subnet. Switching networks applies that network's address and DNS settings. New device-generated certificates include all configured static IPs; changing IPv4 alone preserves existing TLS credentials.

The same complete YAML is used by the setup API while the temporary AP is open:

| Endpoint | Behavior |
| --- | --- |
| `GET /api/v2/setup` | Return normalized YAML and ETag without a token |
| `PUT /api/v2/setup` | Import full YAML with `Content-Type: application/yaml` and the current `If-Match`; return normalized YAML and ETag |
| `POST /api/v2/setup/restart` | Return 200 and schedule a device restart |
| `POST /api/v2/setup/reset` | Mark configuration for reset and restart into the Setup AP; replace both YAML slots with defaults and new tokens at boot |

Setup imports do not restart automatically; the setup page offers restart after a successful save, leaving time to download credentials. Reset restarts immediately and skips shared Wi-Fi import. The normal HTTPS `GET` and `PUT /api/v2/config` and `POST /api/v2/restart` require the admin token. The setup API is available only on the temporary AP; the physical AP password is its access control. `make provision` writes the same full YAML over Iris only while the device is unconfigured or physical Setup is open.

`PUT /api/v2/tokens/admin` accepts a JSON body up to 512 bytes: `{"token":"new-admin-token-0001"}`. `POST /api/v2/tokens/collector` needs no body and generates a 64-character hexadecimal token. Both authenticate with the current admin token and return updated YAML and ETag. Replaced tokens stop working immediately.

## Screen configuration

`screen.pages` holds 1–4 pages, each with required `id` and `widgets` and at most 8 widgets. Empty pages use `widgets: []`. `default_page` defaults to the first page and must reference an existing page. Page IDs are globally unique; widget IDs are unique within their page. Both contain 1–32 bytes from `[A-Za-z0-9._-]`. Titles accept up to 48 UTF-8 bytes; page titles default to the page ID.

The content grid has 4 columns and 6 rows. Required `at: [column, row]` uses zero-based coordinates; required `size: [columns, rows]` gives occupied cells. Each pair contains exactly two integers. Widgets must fit the grid without overlap. Widget IDs default to type plus position, such as `agents-1`; titles default to `Agents`, `Metric`, `Status`, or `Machine`. Clock and weather titles are automatic unless specified.

| Type | Minimum size | Binding |
| --- | --- | --- |
| `clock` | 1 × 1 | Device time; optional `utc_offset` and `seconds`; no host or source |
| `weather` | 1 × 1 | Required `city`, `latitude` and `longitude`; device queries Open-Meteo |
| `metric` | 1 × 1 | Required `host`, `source`, `key`; optional `unit` checks the reported unit |
| `status` | 1 × 1 | Required `host`, `source`; shows status text and level, with detail when space permits |
| `machine` | 2 × 2 | Required `host`; shows `system.cpu/usage_pct` and `system.memory/usage_pct` |
| `agents` | 3 × 3 | `hosts`, `source`, and optional filters |

Binding IDs use the same format as snapshot IDs. Metric `unit` defaults to an empty string (accept any unit); otherwise it contains at most 16 printable non-whitespace ASCII bytes.

Clocks show 24-hour time and add an ISO date when at least two rows tall. `utc_offset` is a quoted `±HH:MM` string from `-12:00` to `+14:00`, default `+00:00`; it is a fixed offset with no automatic daylight-saving adjustment. `seconds` defaults to false. Each clock can use a different offset. An omitted or empty `title` displays `Time (UTC+8)`, `Time (UTC+5:45)`, or `Time (UTC)`, according to the offset, shortened to `Time` if it does not fit. A custom title is displayed as entered. The web preview uses the browser's current time with that offset.

```yaml
- type: clock
  at: [0, 0]
  size: [2, 2]
  utc_offset: '+08:00'
  seconds: true
```

The board synchronizes time through NTP while a clock or weather widget exists on any page. `device.ntp_server` defaults to `pool.ntp.org`; specify a hostname or IP address, up to 127 ASCII characters. NTP uses UDP port 123. Before time is available, clocks show `--:--`. After synchronization, time continues during network outages. Power loss requires synchronization again. Removing all widgets that require time stops NTP.

Weather widgets display Celsius temperature, adding weather conditions when wider or taller than one cell and relative humidity when at least two rows tall. `city` contains 1–64 UTF-8 bytes and supplies the default title. Latitude ranges from −90 to 90; longitude ranges from −180 to 180. The web editor searches cities and stores the selected coordinates. Its preview shows placeholders; the board queries [Open-Meteo](https://open-meteo.com/) directly every fifteen minutes. These are model-based current conditions. Matching coordinates share a cache across pages. A failed update keeps the last values and marks them stale; data also becomes stale after thirty minutes. Failures retry after one minute.

```yaml
- type: weather
  city: Shanghai
  latitude: 31.22222
  longitude: 121.45806
  at: [0, 0]
  size: [2, 2]
```

Weather needs Internet access and synchronized time for HTTPS certificate validation. It uses Open-Meteo's public API without a key; see the provider's [terms](https://open-meteo.com/en/terms) for usage and attribution.

Agents default to all hosts (`hosts: ["*"]`), `source: herdr`, `filter: all`, and `auto_page: true`. `hosts: []` selects no hosts; explicit lists hold at most 4 unique host IDs. `filter` accepts `all`, `attention` (blocked), or `working`. Optional `project` and `agent` are exact matches, limited to 48 and 96 UTF-8 bytes respectively. Lists page every 10 seconds when needed; manual paging and filtering restart the timer. Each widget maintains its own page and filters.

Bindings may reference data that has not arrived yet and survive data removal. Only widgets on the current page create display objects; configured weather locations retain their caches across page changes. Data refreshes do not change layout. The initial layout contains one full-page agents widget for all hosts.

Status widgets distinguish business status from collection health. Collection errors, stale data and offline hosts replace the business text. Status payloads are allocated only for sources of that kind.

Numeric formatting is shared by the device and web preview. Values use one decimal with trailing `.0` removed; counts use integers. The device uses ASCII unit labels.

| Unit | Display |
| --- | --- |
| `bytes`, `bytes_per_second` | Automatic B/KiB/MiB/GiB/TiB/PiB, with `/s` for rates |
| `percent` | `%` |
| `celsius` | `C` |
| `watts` | W/kW/MW |
| `hertz` | Hz/kHz/MHz/GHz |
| `seconds`, `milliseconds` | ms/s/min/h/d |
| `count`, `number` | No suffix |
| Other units | Reported unit as suffix |

## Alerts

Defaults are `volume: 35`, `vibration_strength: 60`, `flash_enabled: false`, and `rules: []`. Up to 8 rules are allowed. IDs default to `rule-1`, `rule-2`, etc.; they must be unique and contain 1–48 bytes from `[A-Za-z0-9._-]`. `enabled` defaults to true.

| Type | Required binding and condition |
| --- | --- |
| `agent_done` | `source` defaults to `herdr` and may name any `agents` source. A previously observed agent changes from `working` to `done`, or to `idle` when `include_idle` is true (the default). First observation establishes a baseline. |
| `metric_high` | Required `host`, `source`, `key`, `unit`, `threshold`, and `recover`. A fresh matching-unit value stays strictly above `threshold` for `hold` seconds. A value at or below `recover` rearms it; `recover` must be below `threshold`. Missing or stale samples interrupt the timer. |
| `host_offline` | A previously observed online host remains offline for `hold` seconds, after the initial 30-second report timeout. Wi-Fi loss suspends detection; the host must be seen online again to rearm. |

`host` defaults to `"*"` for agent and offline rules. Metric rules require one explicit host. Unreported bindings are allowed. Source IDs and keys contain 1–48 bytes from `[A-Za-z0-9._-]`; units contain 1–16 printable non-whitespace ASCII bytes. Thresholds are finite numbers. Metric `hold` defaults to 60 seconds (range 1–86400); offline `hold` defaults to 30 (range 5–86400).

`duration` defaults to 10 seconds (range 0–300); zero requires manual dismissal. `sound`, `vibrate`, and `flash` are independent booleans, default false. Global volume and vibration strength are integers from 0 to 100; zero disables that output. Flash requires both the rule's `flash` and global `flash_enabled`.

The RAM queue holds 8 notifications without history. Warnings precede pending informational notifications but never interrupt the current notification. Rule/host/agent duplicates are suppressed. Metric recovery and host recovery remove corresponding pending notifications. Dismissal or timeout advances the queue. Changing rules clears notifications and baselines; changing only output levels or the global flash switch preserves them. Level changes update queued notifications.

Diagnostics expose `alert_id`, `alerts_pending`, and `alerts_dropped`. Each of `audio_*`, `vibration_*`, and `flash_*` has `queued`, `played`, `failed`, `dropped`, and `busy` fields. `played` records completed driver calls. `dropped` counts full queues and queued outputs suppressed by a later mute or flash-disable change; outputs suppressed before enqueue are not counted. `audio_muted` and `vibration_muted` are temporary RAM switches reset at reboot. Settings output tests bypass alert rules and update only output counters.

## Device settings and persistence

`device` defaults to `brightness: 75`, `rotation: 0`, `auto_rotate: false`, and `ntp_server: pool.ntp.org`. Brightness is an integer from 0 to 100; rotation is 0, 90, 180, or 270 degrees. Saved settings apply immediately.

Automatic rotation uses BMI270 acceleration and requires 500 ms of stable orientation. Flat placement, boundary positions, motion, and sample failures retain the current direction. Turning it off saves the actual direction to `rotation`; the response includes that value. Automatic direction changes do not change ETags or write Flash. `diagnostics.orientation` reports actual `rotation` and `imu` (`disabled`, `ok`, or `unavailable`), plus `accel_g:[x,y,z]` and `sample_age_ms` for valid samples.

Device settings, layout, alerts, and credentials share one YAML document in the application's NVS `curio` namespace, with two alternating slots and an internal revision. Boot selects the newer valid slot and retains the previous valid copy. Reporting, paging, notifications, and state refreshes do not write Flash. Device Settings and the web interface use the same configuration and ETag. See [firmware.md](firmware.md) for device controls and deployment.
