# Curio

[English](#english) · [中文](#中文)

## English

A general-purpose status display for ESP-Mosaico. Collectors choose what to report; the board hosts the Hub, web configuration and LVGL display. Collection and display are configured independently.

Collectors send machine and application state. The board reads its own clock and sensors; these do not require a Collector.

```text
Local YAML → Collector → HTTPS → Mosaico Hub → Display
                                    ↑
                              Web configuration
```

- Receive selected data sources from up to four machines.
- Arrange clocks, metrics, status cards, single-machine CPU/memory cards and Agent lists on a 4 × 6 grid. Widgets can be moved and resized; only the current page is instantiated.
- Clocks use network time directly on the board, with a UTC offset per widget and optional seconds.
- Swipe Agent lists or enable ten-second automatic paging.
- Configure completion, high-metric and offline alerts, with independent sound, vibration and camera-module flash.
- Adjust rotation, brightness and feedback on the device. Battery status comes from the BQ27220.
- Edit, import and export Hub configuration as YAML. Collectors use their own local YAML files.

### Supported collectors

The included `curio-collector` has five built-in modules. Each machine selects its sources and metric fields in local YAML.

| Category | Module | Data |
| --- | --- | --- |
| System metrics | `cpu` | CPU usage and logical CPU count |
| System metrics | `memory` | Memory usage, used, available and total bytes |
| System metrics | `disk` | Usage and capacity for the filesystem containing a selected path |
| System metrics | `network` | Receive and transmit rates for a selected interface |
| Application state | `herdr` | Agent names, brands, projects and states from herdr |

The [snapshot protocol](docs/protocol.md) also accepts custom reporters that publish metrics, status text or agent lists. Each source declares its data lifetime independently of host connectivity. Numeric widgets format capacities, rates and common units automatically.

OS compatibility is listed per module in the [Collector guide](docs/collector.md#supported-modules-and-platforms). System metrics have been verified on Linux and Windows 10; macOS collection and Windows herdr integration remain unverified. Service installation supports Linux systemd and Windows services.

`make collector-windows` builds `build/windows/curio-collector.exe` for Windows amd64. Double-click it for setup, or use `curio-collector.exe run` for foreground collection. See [Windows setup](docs/collector.md#windows-setup).

### Start

Build and install the device firmware using the [firmware guide](docs/firmware.md). For first setup, join the temporary `Curio-…` Wi-Fi network with the password shown on the board and open `http://192.168.4.1/setup.html`. Enter your Wi-Fi settings, save, download the certificate and full YAML, then select **Connect** to restart. Alternatively, run `make configure SSID="YourWiFi"` followed by `make provision` over USB or paired Wi-Fi while the device is unconfigured or its physical Setup AP is open. Then build the Collector with Go 1.22+ and Make from the repository root:

```sh
make collector
./build/curio-collector init
```

Edit the generated `collector.yaml`: set the endpoint, certificate, token file and sources. See the [example](collector/config.example.yaml) and [Collector guide](docs/collector.md).

```sh
./build/curio-collector check
./build/curio-collector --dry-run
./build/curio-collector
```

The default file is `curio/collector.yaml` in the user's configuration directory. Use `--config PATH` to select another file; restart after edits. On Linux, install a user service with `make install-collector`, or use `make install-collector MODE=system` for a system service. See [service deployment](docs/collector.md#services).

Open `https://curio.local` (or your chosen hostname) in a browser, trust the device certificate and sign in with the admin token from setup. Computer provisioning also saves it in `firmware/local/admin-token.txt`. The web interface edits screen layout, data bindings, alerts, network settings and YAML. Bindings may refer to data that has not arrived yet. The [example Hub configuration](curio.example.yaml) shows the format.

Admin and Collector tokens can be changed from the web interface. Full YAML exports contain Wi-Fi settings, both tokens, and the TLS certificate and private key. Network and certificate changes take effect after a restart. Alert rules are empty by default; notifications disappear after ten seconds unless configured otherwise. Reported data and notifications are kept in RAM.

### Commands

Run `make help` for build, flash, service and test commands. `make test` runs the host tests; UI and browser tests have separate targets described in the [test guide](tests/README.md).

`make flash` builds before installing and preserves saved NVS configuration. `make flash BUNDLE=/path/to/file.irisfw` installs an existing bundle. `make configure` writes credentials to `firmware/local/`; normal firmware builds do not include them. `make provision` writes that configuration to the device under the setup gate. Firmware and Collector output defaults to `build/`. Set `PYTHON`, `GO` or `FIRMWARE_BUILD_DIR` when using a different interpreter, toolchain or build location.

### Limits and development

The Hub accepts four hosts, eight sources per host, and a total of 32 metrics and 16 agents per host. It supports four screen pages with eight widgets each. The device font is ASCII; other characters display as `?`. The web interface retains Unicode. Weather, historical charts and agent control are not implemented.

| Path | Contents |
| --- | --- |
| `collector/` | Collection modules, local configuration, Windows setup window and HTTPS reporting |
| `firmware/components/` | Portable C state, YAML, layout and alert models; dependency adapters |
| `firmware/main/` | ESP-IDF networking, persistence, sensors, outputs, LVGL interface and web assets |
| `Makefile` | Build, flash, install and test commands |
| `scripts/firmware/` | Firmware configuration, build and official device-tool adapter |
| `scripts/collector/` | Linux service installation and removal |
| `scripts/dev/` | Local development Hub and C bridge |
| `tests/` | Host, browser and integration tests |

See the [protocol](docs/protocol.md), [device controls](docs/firmware.md#runtime-and-controls) and [tests](tests/README.md). Dependencies are pinned in `dependencies.json`, `firmware/dependencies.lock` and `collector/go.sum`. Build output, credentials and local tool state are excluded from Git.

## 中文

Curio 是运行在 ESP-Mosaico 上的通用状态屏。Collector 自行选择上报内容；开发板运行 Hub、网页配置和 LVGL 界面。采集与展示独立配置。

Collector 上报机器和应用状态。开发板自行读取时钟和传感器，无需 Collector。

- 接收最多四台机器各自选择上报的数据来源。
- 使用 4 列 × 6 行网格排布时钟、数值、状态卡、单机 CPU/内存卡和 Agent 列表，支持移动、缩放；只创建当前页的组件。
- 时钟由开发板直接网络校时，每个组件可设置 UTC 偏移和秒数显示。
- Agent 列表支持滑动和可关闭的十秒自动翻页。
- 支持 Agent 完成、指标过高和机器离线提醒，声音、振动和相机模块闪光可分别配置。
- 设备上可调整方向、亮度和提醒输出，通过 BQ27220 显示电池状态。
- Hub 配置支持 YAML 编辑、导入和导出；Collector 使用各自的本地 YAML。

### 支持的采集器

目前提供一个 `curio-collector` 程序，内置五个采集模块。每台机器通过本地 YAML 选择来源和指标字段。

| 分类 | 模块 | 采集内容 |
| --- | --- | --- |
| 系统指标 | `cpu` | CPU 使用率、逻辑 CPU 数量 |
| 系统指标 | `memory` | 内存使用率、已用、可用和总字节数 |
| 系统指标 | `disk` | 指定路径所在文件系统的使用率和容量 |
| 系统指标 | `network` | 指定网卡的接收、发送速率 |
| 应用状态 | `herdr` | herdr 中 Agent 的名称、类型、项目和状态 |

[快照协议](docs/protocol.md)也允许自定义采集器上报数值指标、状态文本或 Agent 列表。每个来源可设置数据有效期，与主机在线状态独立判断。数值组件自动格式化容量、速率和常见单位。

操作系统兼容性按模块列在 [Collector 文档](docs/collector.md#supported-modules-and-platforms)中。系统指标已在 Linux 和 Windows 10 验证；macOS 采集和 Windows herdr 集成仍待验证。服务安装支持 Linux systemd 和 Windows 服务。

`make collector-windows` 生成 Windows amd64 程序 `build/windows/curio-collector.exe`。双击打开配置窗口，或执行 `curio-collector.exe run` 在前台采集。参见 [Windows 配置](docs/collector.md#windows-setup)。

### 开始使用

按[固件文档](docs/firmware.md)构建并安装设备固件。首次配置时，用设备屏幕显示的随机密码连接 `Curio-…` 临时 Wi-Fi，打开 `http://192.168.4.1/setup.html`，填写 Wi-Fi 信息并保存，下载证书和完整 YAML 后点击「连接」重启。也可在设备尚未配置或已手动开启设置热点时，用 `make configure SSID="YourWiFi"` 和 `make provision` 通过 USB 或已配对 Wi-Fi 写入配置。随后准备 Go 1.22+ 和 Make，在仓库根目录构建 Collector：

```sh
make collector
./build/curio-collector init
```

编辑生成的 `collector.yaml`，填写设备地址、证书、token 文件和采集来源，参见[配置示例](collector/config.example.yaml)与 [Collector 文档](docs/collector.md)。

```sh
./build/curio-collector check
./build/curio-collector --dry-run
./build/curio-collector
```

默认配置位于用户配置目录下的 `curio/collector.yaml`，可用 `--config PATH` 指定其他路径，修改后重启。Linux 可执行 `make install-collector` 安装用户服务，或使用 `make install-collector MODE=system` 安装系统服务，参见[服务部署](docs/collector.md#services)。

浏览器打开 `https://curio.local`（或设置的主机名），信任设备证书后使用设置时取得的管理 token 登录。电脑配置时也会写入 `firmware/local/admin-token.txt`。网页可配置布局、数据绑定、提醒、网络和完整 YAML，也能预先绑定尚未上报的数据。[Hub 配置示例](curio.example.yaml)展示了字段格式。

网页可修改管理 token、生成 Collector token；完整 YAML 导出包含 Wi-Fi、两项 token、证书和私钥。修改网络或证书后需重启生效。提醒默认没有规则，需要自行添加；通知默认十秒消失，也可调整。采集数据和通知仅保存在 RAM。

### 常用命令

执行 `make help` 查看构建、刷写、服务和测试命令。`make test` 运行主机测试；UI 和浏览器测试使用单独入口，见[测试文档](tests/README.md)。

`make flash` 先构建再刷写，并保留设备 NVS 中已有的配置；`make flash BUNDLE=/path/to/file.irisfw` 刷写已有升级包。`make configure` 将凭证写到 `firmware/local/`，普通固件构建不包含凭证；`make provision` 仅在设置入口开放时写入设备。固件和 Collector 构建产物默认位于 `build/`。可用 `PYTHON`、`GO`、`FIRMWARE_BUILD_DIR` 指定解释器、工具链或构建目录。

### 限制与开发

Hub 最多接收四台主机，每机八个来源、合计 32 个指标和 16 个 Agent；最多四个展示页，每页八个组件。设备字体仅支持 ASCII，其他字符显示为 `?`，网页保留 Unicode。尚未实现天气、历史曲线和 Agent 控制。

`Makefile` 统一命令入口。`collector/` 负责采集和上报；`firmware/components/` 存放可在主机测试的状态、YAML、布局和提醒模型；`firmware/main/` 接入 ESP-IDF、设备驱动、LVGL 和网页。`scripts/` 按 `firmware`、`collector`、`dev` 分组存放构建、部署和开发脚本，`tests/` 存放测试。详细说明见[协议](docs/protocol.md)和[设备操作](docs/firmware.md#runtime-and-controls)。

依赖由 `dependencies.json`、`firmware/dependencies.lock` 和 `collector/go.sum` 固定。构建产物、凭证和本地运行数据不纳入 Git。
