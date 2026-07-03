# Esp32RemoteAgent

ESP32-S3 远程桌面中转系统，用于现场终端设备无法直接联网，但运维人员需要远程桌面访问终端设备的场景。

系统使用 ESP32-S3 板子作为现场侧 USB 网络桥接设备，使用具备公网访问能力的中转服务器作为运维入口。运维 PC 通过远程桌面连接中转服务器上的指定端口，中转服务器再把 TCP 数据流通过 ESP32-S3 板子转发到终端设备的 RDP 端口。

## 系统架构

```text
运维 PC
    |
    | 远程桌面连接 relay-server-ip:6500-6600
    v
中转服务器（.NET 8，公网 IP）
    |
    | 板子控制通道，TCP 6555
    v
ESP32-S3 板子
    |
    | USB 虚拟网卡，192.168.77.0/24
    v
终端设备
    |
    | Windows 远程桌面服务
    v
TCP 3389
```

## 代码组成

- `Code/ESP-IDF/Esp32RemoteAgent`
  - ESP-IDF 5.3.x 固件，目标芯片为 ESP32-S3。
  - WiFi STA 模式，板子自动连接现场 WiFi。
  - TinyUSB USB 虚拟网卡，用于和终端设备建立 USB 网络。
  - 中转服务器控制通道和 TCP 数据转发。
  - RGB 状态灯。

- `Code/RelayServer`
  - .NET 8 中转服务。
  - SQLite 数据持久化。
  - Vue 3 静态管理后台页面。
  - 管理员登录。
  - HTTPS 发布和启动脚本。

## 主要功能

- 固件配置固定 WiFi 账号和密码。
- 板子使用 `boardId` 和 `boardKey` 注册到中转服务器。
- 公网远程桌面端口由中转服务器后台配置，并通过 TCP `6555` 控制通道下发给板子。
- 新固件使用 HMAC-SHA256 注册鉴权，同时兼容旧版本明文 `authKey` 注册方式。
- 固件启动配置支持 NVS 持久化。
- 中转服务器固定控制端口：TCP `6555`。
- 板子公网远程桌面端口：通常为 `6500-6600`。
- 终端设备 RDP 地址：`192.168.77.2:3389`。
- 运维 PC 使用远程桌面连接 `relay-server-ip:assigned-port`。
- 管理后台支持板子和事件管理。
- 管理后台诊断信息包括 RSSI、剩余堆内存、固件版本、心跳、流量计数和目标端口探测。
- SQLite 保存最近的中转事件。
- 管理员登录失败限流。
- 支持通过自签名证书启用 HTTPS。
- RGB 状态灯：
  - 红灯：未连接中转服务器。
  - 绿灯：已连接并注册到中转服务器。
  - 蓝灯闪烁：正在转发隧道数据。

## 开发环境

推荐开发环境：

- Windows 10/11
- Git for Windows
- PowerShell 5.1 或 PowerShell 7
- .NET SDK 8.x，用于中转服务器
- ESP-IDF 5.3.x，用于 ESP32-S3 固件
- ESP-IDF 安装器自带的 Python 环境
- ESP-IDF 工具安装目录：`C:\Espressif`
- ESP32-S3 USB 串口驱动，已测试板子使用 `COM5`

已测试的本地 ESP-IDF 目录结构：

```text
C:\Espressif\frameworks\esp-idf-v5.3.1-2
C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe
C:\Espressif\tools
```

Windows 下 ESP-IDF 注意事项：

- 不建议直接在包含中文或其他非 ASCII 字符的项目路径下编译固件，ESP-IDF/Kconfig 在路径转换时可能失败。
- 推荐使用项目提供的固件脚本。脚本会把固件项目复制到 `C:\tmp\Esp32RemoteAgentBuild`，在临时目录注入运行配置，然后从纯 ASCII 路径编译和烧录。
- 不要提交生成的 `sdkconfig`、`dependencies.lock`、`managed_components`、`build`、WiFi 密码、板子密钥、数据库、证书或日志文件。

检查服务端工具：

```powershell
dotnet --version
dotnet build Code\RelayServer\RelayServer.sln
```

检查固件工具：

```powershell
C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe `
  C:\Espressif\frameworks\esp-idf-v5.3.1-2\tools\idf.py --version
```

## 快速开始

使用推荐脚本编译并烧录固件：

```powershell
.\Code\ESP-IDF\Esp32RemoteAgent\flash-firmware.ps1 `
  -WifiSsid "YOUR_WIFI_SSID" `
  -WifiPassword "YOUR_WIFI_PASSWORD" `
  -ServerHost "YOUR_RELAY_SERVER_IP" `
  -Port "COM5" `
  -BoardId "S3-0001" `
  -BoardKey "CHANGE_THIS_DEVICE_SECRET"
```

中转服务器地址会由 `flash-firmware.ps1` 写入临时构建目录中的固件配置，再编译烧录到板子。`ServerHost` 支持 IPv4 地址或域名，例如 `soft.mybips.com`。可以直接修改脚本参数默认值：

```powershell
[string]$ServerHost = "YOUR_RELAY_SERVER_IP"
```

也可以在执行脚本时通过 `-ServerHost` 覆盖。烧录前必须把 `ServerHost` 和 `BoardKey` 改成真实值，否则脚本会停止，避免把占位配置烧进板子。

脚本只会把真实 WiFi 和服务器地址写入 `C:\tmp` 下的临时构建目录，不会修改仓库中被 Git 跟踪的固件配置文件。

只编译不烧录：

```powershell
.\Code\ESP-IDF\Esp32RemoteAgent\flash-firmware.ps1 `
  -WifiSsid "TEST_SSID" `
  -WifiPassword "TEST_PASSWORD" `
  -ServerHost "192.0.2.1" `
  -BuildOnly
```

只烧录已有构建产物：

```powershell
cd C:\tmp\Esp32RemoteAgentBuild
C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe `
  C:\Espressif\frameworks\esp-idf-v5.3.1-2\tools\idf.py `
  -p COM5 flash
```

这个方式适用于已经通过 `flash-firmware.ps1` 编译过固件，并且 `C:\tmp\Esp32RemoteAgentBuild` 目录和其中的 `build` 产物还存在时。它只把现有构建产物重新烧录到板子，不会重新注入 WiFi、中转服务器地址、`BoardKey` 或其他配置。修改过任何配置或代码后，应重新运行 `flash-firmware.ps1` 编译并烧录。

修改 WiFi、中转服务器地址或 `BoardKey` 后重新烧录：

```powershell
.\Code\ESP-IDF\Esp32RemoteAgent\flash-firmware.ps1 `
  -WifiSsid "YOUR_WIFI_SSID" `
  -WifiPassword "YOUR_WIFI_PASSWORD" `
  -ServerHost "YOUR_RELAY_SERVER_IP_OR_DOMAIN" `
  -BoardKey "YOUR_DEVICE_SECRET" `
  -Port "COM5" `
  -EraseFlash
```

固件会把首次启动配置写入 NVS，并在后续启动时优先读取 NVS。只执行普通 `flash` 不会清除 NVS，所以如果板子之前已经保存过 `soft.mybips.com`，即使新的 `sdkconfig.defaults` 里是 `1.12.218.7`，启动日志仍可能继续显示旧服务器地址。`-EraseFlash` 会清除 NVS，让板子重新采用本次编译烧录进去的 WiFi、服务器地址和 `BoardKey`。适用场景是更换中转服务器、修改 WiFi、修改板子密钥、或排查“烧录后仍读取旧配置”的问题。

仅当项目路径为纯 ASCII 路径时，才建议手动编译固件：

```powershell
cd Code\ESP-IDF\Esp32RemoteAgent
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
idf.py -p COM5 flash
```

开发模式运行中转服务器：

```powershell
cd Code\RelayServer
dotnet restore
dotnet run --project .\RelayServer.csproj
```

发布并以 HTTPS 启动中转服务器：

```powershell
cd Code\RelayServer
.\publish-start.ps1
```

## 安全说明

- 不要提交真实 WiFi 密码、板子密钥、生成的证书、SQLite 数据库或运行日志。
- 部署前必须修改开发环境默认管理员密码。
- 生产环境建议使用可信 CA 证书，不建议长期使用自签名证书。
- 中转服务器应通过防火墙只开放必要端口。
- 每块板子应使用独立的 `boardKey`，不要批量复用默认占位密钥。

## 当前限制

- 当前已实现 RDP TCP 转发。RDP UDP 转发后续可以增加，用于进一步改善远程桌面体验。
- USB 虚拟网卡兼容性取决于终端 Windows 系统的 USB 网络驱动支持。当前实现为 NCM，部分 Windows 镜像可能需要 RNDIS。
- 中转服务器默认使用 SQLite，适合轻量部署；大规模设备管理时可以扩展为 PostgreSQL 或 SQL Server。

## 更多文档

- [ESP-IDF 固件说明](Code/ESP-IDF/Esp32RemoteAgent/README.md)
- [AI 后续开发指南](docs/AI_DEVELOPMENT_GUIDE.md)

## 开源协议

本项目使用 MIT License 开源，详见 [LICENSE](LICENSE)。
