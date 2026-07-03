# AI 后续开发指南

本文档面向后续接手本项目的 AI 或开发者。目标是减少重复试错，尤其是 ESP-IDF 在 Windows 中文路径、固件烧录、中转协议、服务端结构和安全边界上的问题。

## 项目目标

本项目用于让无法直接联网的 Windows 终端设备接受远程桌面访问。

整体链路：

```text
运维 PC
  |
  | RDP: relay-server-ip:assigned-port
  v
中转服务器 .NET 8
  |
  | TCP 6555 控制通道
  v
ESP32-S3 板子
  |
  | USB 虚拟网卡 192.168.77.0/24
  v
终端设备 192.168.77.2:3389
```

当前已经实现 RDP TCP 转发，实测可以从运维 PC 远程桌面到终端设备。RDP UDP 转发尚未实现，后续可作为性能优化方向。

## 目录结构

```text
README.md
LICENSE
docs/
  AI_DEVELOPMENT_GUIDE.md
Code/
  ESP-IDF/
    Esp32RemoteAgent/
      flash-firmware.ps1
      sdkconfig.defaults
      main/
        main.c
        remote_config.c/.h
        status_led.c/.h
        Kconfig.projbuild
        idf_component.yml
  RelayServer/
    RelayServer.sln
    Program.cs
    Relay/
    Data/
    Endpoints/
    Models/
    wwwroot/
    publish-start.ps1
```

核心入口：

- 固件主程序：[main.c](../Code/ESP-IDF/Esp32RemoteAgent/main/main.c)
- 固件配置/NVS：[remote_config.c](../Code/ESP-IDF/Esp32RemoteAgent/main/remote_config.c)
- RGB 状态灯：[status_led.c](../Code/ESP-IDF/Esp32RemoteAgent/main/status_led.c)
- 固件烧录脚本：[flash-firmware.ps1](../Code/ESP-IDF/Esp32RemoteAgent/flash-firmware.ps1)
- 中转服务器入口：[Program.cs](../Code/RelayServer/Program.cs)
- 中转核心：[Relay](../Code/RelayServer/Relay)
- 数据访问：[Data](../Code/RelayServer/Data)
- API 端点：[Endpoints](../Code/RelayServer/Endpoints)
- 管理页面：[wwwroot](../Code/RelayServer/wwwroot)

## 当前协议

ESP32-S3 和中转服务器之间保持一条 TCP 控制连接，默认端口为 `6555`。

帧格式：

```text
byte 0      : frame type
byte 1-4    : connection id, uint32 big-endian
byte 5-8    : payload length, uint32 big-endian
byte 9...   : payload bytes
```

帧类型：

| Type | 名称 | 方向 | 说明 |
|---:|---|---|---|
| 1 | Register | 板子 -> 服务器 | 注册板子 |
| 2 | RegisterAck | 服务器 -> 板子 | 注册成功，并下发运行配置 |
| 3 | Heartbeat | 板子 -> 服务器 | 心跳和诊断信息 |
| 4 | Open | 服务器 -> 板子 | 打开终端 TCP 连接 |
| 5 | Data | 双向 | 转发 TCP 数据 |
| 6 | Close | 双向 | 关闭连接 |
| 7 | Error | 板子 -> 服务器 | 板子侧错误 |

协议修改必须同时修改固件和服务器：

- 固件：`send_frame`、`register_board`、`wait_register_ack`、`process_relay_frame`、`handle_data`
- 服务器：`RelayFrame.cs`、`RelayFrameType.cs`、`ControlChannelService.cs`、`BoardSession.cs`

## 注册和端口下发

公网远程桌面端口不再固定写在固件里。

当前流程：

1. 板子连接中转服务器 TCP `6555`。
2. 板子发送 `Register`，其中 `assignedPort=0`，表示请求服务器下发端口。
3. 服务器根据 `boardId` 从 SQLite 读取该板子的 `AssignedPort`、`TargetHost`、`TargetPort`。
4. 服务器校验 HMAC-SHA256 注册签名。
5. 服务器返回 `RegisterAck`：

```json
{
  "ok": true,
  "assignedPort": 6500,
  "targetHost": "192.168.77.2",
  "targetPort": 3389
}
```

6. 固件保存运行时端口和目标地址，后续心跳会上报 `assignedPort`。

兼容逻辑：

- 如果旧固件仍发送 `assignedPort > 0`，服务器会校验该端口是否和后台配置一致。
- 如果新固件发送 `assignedPort=0`，服务器以后台数据库配置为准。
- `CONFIG_REMOTE_AGENT_ASSIGNED_PUBLIC_PORT` 现在只是旧工具和文档兼容字段，不应再作为新固件的固定公网端口来源。

## 固件实现要点

入口函数：`app_main`

当前固件已经开始从单一 `main.c` 拆分：

- `main.c`：保留启动顺序、WiFi、USB NCM、relay 连接和 tunnel 调度。后续应继续拆小。
- `remote_config.c/.h`：编译期配置、NVS 首次落地、NVS 读取。
- `status_led.c/.h`：RGB 状态灯初始化、连接状态、数据蓝灯闪烁。

后续推荐继续拆分顺序：

1. `relay_protocol.c/.h`：帧读写、JSON 小工具、HMAC。
2. `usb_net.c/.h`：TinyUSB NCM 描述符、USB netif、收发回调。
3. `relay_client.c/.h`：中转服务器注册、心跳、重连主循环。
4. `tunnel.c/.h`：终端 TCP 连接表、读写泵、关闭逻辑。

启动顺序：

1. 初始化 NVS。
2. 加载配置，首次启动会把编译期配置写入 NVS，后续优先读取 NVS。
3. 初始化 RGB 状态灯。
4. 初始化 tunnel 数组。
5. 启动 WiFi STA。
6. 启动 USB NCM 虚拟网卡。
7. 在 Core 1 上启动 `relay_task`。

重要配置来自 `sdkconfig.defaults` 或 `idf.py menuconfig`：

- `CONFIG_REMOTE_AGENT_WIFI_SSID`
- `CONFIG_REMOTE_AGENT_WIFI_PASSWORD`
- `CONFIG_REMOTE_AGENT_SERVER_HOST`
- `CONFIG_REMOTE_AGENT_SERVER_CONTROL_PORT`
- `CONFIG_REMOTE_AGENT_BOARD_ID`
- `CONFIG_REMOTE_AGENT_BOARD_KEY`
- `CONFIG_REMOTE_AGENT_ASSIGNED_PUBLIC_PORT`
- `CONFIG_REMOTE_AGENT_TERMINAL_RDP_HOST`
- `CONFIG_REMOTE_AGENT_TERMINAL_RDP_PORT`

仓库中的 `sdkconfig.defaults` 必须保持占位值，不要提交真实 WiFi 密码、服务器地址或板子密钥。

## USB 虚拟网卡

当前使用 TinyUSB NCM：

- ESP32 USB 侧 IP：`192.168.77.1/24`
- 终端设备 IP：DHCP 或手动 `192.168.77.2/24`
- 终端 RDP：`192.168.77.2:3389`

相关函数：

- `startUsbNetwork`
- `usb_recv_callback`
- `usb_netif_transmit`

如果 Windows 不出现虚拟网卡，优先检查：

- 是否连接 ESP32-S3 原生 USB OTG 口。
- USB 线是否支持数据。
- 设备管理器是否有未知 USB 设备。
- Windows 是否缓存了旧 PID 或旧描述符。
- 当前 Windows 镜像是否支持 NCM；部分系统可能需要改为 RNDIS。

## 双核和任务分配

ESP32-S3 是双核。当前实现中：

- WiFi 驱动任务由 ESP-IDF 管理。
- 主启动流程在 `app_main` 所在核心执行。
- `relay_task` 固定运行在 Core 1，用于控制连接、心跳、隧道帧处理。
- USB 网络回调由 TinyUSB/网络栈触发。

后续如果继续优化性能，建议把接收、发送、USB 转发拆成更明确的 FreeRTOS 任务，并使用队列减少单任务轮询延迟。

## 状态灯

RGB 状态灯语义：

- 红灯：未连接中转服务器。
- 绿灯：已连接并注册成功。
- 蓝灯闪烁：有隧道数据转发。

如果换板子或换引脚，先检查 `status_led_init` 和 LED GPIO 配置。

## 性能优化现状

已完成的低风险优化：

- 固件 `MAX_FRAME_PAYLOAD` 调整为 `8192`。
- 固件终端连接和服务器连接设置 `TCP_NODELAY`。
- 固件关闭 WiFi 省电：`esp_wifi_set_ps(WIFI_PS_NONE)`。
- 服务端 `RelayFrame.WriteAsync` 移除每帧 `FlushAsync()`。
- 固件心跳上报 RSSI、free heap、active tunnels、上下行字节数、固件版本、运行时 assigned port。
- 服务端统计每块板子的公网流入字节、板子返回字节、最后错误和最近心跳。

仍可继续优化：

- 增加 RDP UDP 3389 转发。
- 拆分板端收发任务，降低单任务轮询延迟。
- 管理后台显示实时上下行速率、延迟和连接质量。
- 服务端按板子统计吞吐、连接数和失败原因。

## 已完成的 P0-P3 优化

P0：

- 固件增加 NVS 配置命名空间 `remote_cfg`。
- 首次启动写入编译期配置，后续启动优先读取 NVS。
- 注册报文支持 HMAC-SHA256 签名，新固件不再把 `authKey` 明文发给服务器。
- 服务器兼容旧固件明文 `authKey` 注册，方便滚动升级。
- 管理后台拒绝保存默认占位密钥 `CHANGE_THIS_DEVICE_SECRET`。
- 板子的公网远程端口由服务器后台配置下发，不再固定在固件里。

P1：

- 固件上报更多运行状态。
- 服务端统计每块板子的转发字节数和诊断字段。
- `flash-firmware.ps1` 增加 `-BuildOnly`，后续 AI 可以只编译不烧录。

P2：

- 管理后台增加诊断区域，展示流量、RSSI、heap、最后心跳和固件版本。
- 新增 `/api/boards/diagnostics` 诊断接口。
- 新增 `/api/boards/{boardId}/probe-target` 服务端侧目标端口测试接口。
- 事件日志写入 SQLite `relay_events` 表，服务重启后仍可查询最近事件。
- 管理页面已经做过一次简化和美化，保持轻量、清晰、面向运维操作。

P3：

- 管理员登录增加基于 IP 和用户名的失败限速。
- Cookie 启用 `HttpOnly` 和 `SameSite=Strict`。
- 发布脚本支持 HTTPS 自签证书流程。
- 项目已添加 MIT License。

## 尚未完成，不能误判为已完成

- RDP UDP 转发尚未实现。
- 控制通道 TLS 尚未实现；当前是 HMAC 注册认证 + 裸 TCP 数据通道。
- 现场免重刷固件的交互式配置工具尚未实现。当前 NVS 可持久化，但主要仍通过烧录脚本注入初始值。
- Windows RNDIS 兼容模式尚未实现，当前是 NCM。
- 大规模多服务器部署、负载均衡和集中数据库尚未实现。

## 固件编译和烧录标准流程

不要直接在本仓库中文路径下运行 `idf.py build`。ESP-IDF 在 Windows 下可能把中文路径转码成乱码，导致 Kconfig 或构建工具找不到文件。

使用脚本：

```powershell
.\Code\ESP-IDF\Esp32RemoteAgent\flash-firmware.ps1 `
  -WifiSsid "YOUR_WIFI_SSID" `
  -WifiPassword "YOUR_WIFI_PASSWORD" `
  -ServerHost "YOUR_RELAY_SERVER_IP" `
  -Port "COM5" `
  -BoardId "S3-0001" `
  -BoardKey "CHANGE_THIS_DEVICE_SECRET"
```

脚本职责：

1. 将固件工程复制到 `C:\tmp\Esp32RemoteAgentBuild`。
2. 只在临时目录修改 `sdkconfig.defaults`，写入 WiFi 和服务器配置。
3. 设置 `PROCESSOR_ARCHITECTURE=AMD64`，避免自动化环境里 ESP-IDF 识别平台为 `Windows-`。
4. 设置 `IDF_TOOLS_PATH=C:\Espressif`。
5. 使用 `C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe` 调用 `idf.py`。
6. 执行 `idf.py -p COMx build flash`。

中转服务器地址必须通过 `flash-firmware.ps1` 配置后再烧录。可以直接修改脚本里的参数默认值：

```powershell
[string]$ServerHost = "YOUR_RELAY_SERVER_IP"
```

也可以执行脚本时传入 `-ServerHost "your-relay-server-ip-or-domain"`。固件连接函数使用 DNS 解析，`ServerHost` 支持 IPv4 地址或域名，例如 `soft.mybips.com`。脚本会在烧录前检查 `ServerHost` 和 `BoardKey`，如果仍是占位值会直接停止，避免错误配置进入板子。

只编译不烧录：

```powershell
.\Code\ESP-IDF\Esp32RemoteAgent\flash-firmware.ps1 `
  -WifiSsid "TEST_SSID" `
  -WifiPassword "TEST_PASSWORD" `
  -ServerHost "192.0.2.1" `
  -BuildOnly
```

已验证的本机环境：

```text
ESP-IDF: C:\Espressif\frameworks\esp-idf-v5.3.1-2
Python:  C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe
Tools:   C:\Espressif\tools
Port:    COM5
Target:  esp32s3
```

脚本可选参数：

```powershell
-Monitor      # 烧录后进入 monitor
-EraseFlash   # 烧录前擦除 flash
-BuildOnly    # 只编译，不烧录
```

注意：

- 不要把真实 WiFi 密码写入仓库文件。
- 不要提交 `sdkconfig`、`dependencies.lock`、`managed_components`、`build`。
- 如果换机器，先确认 ESP-IDF 安装路径和 Python env 路径。

## 中转服务器实现要点

服务器使用 .NET 8、SQLite、静态 Vue 3 页面。

主要模块：

- `Program.cs`：应用启动、服务注册、API 映射。
- `Relay/ControlChannelService.cs`：监听板子控制端口 `6555`，处理注册、鉴权和配置下发。
- `Relay/BoardSession.cs`：每块板子的在线会话和公网端口监听。
- `Relay/RelayFrame.cs`：板子和服务器之间的帧读写。
- `Relay/RelayHub.cs`：在线板子、事件列表和诊断状态。
- `Data/BoardRepository.cs`：板子配置 SQLite 存储。
- `Data/AuthRepository.cs`：管理员账号存储。
- `Endpoints/`：管理 API。
- `wwwroot/`：管理页面。

开发验证：

```powershell
dotnet build Code\RelayServer\RelayServer.sln
```

发布启动：

```powershell
cd Code\RelayServer
.\publish-start.ps1
```

运行产物如 `publish`、`certs`、`relay.db`、日志、PID 文件均不应提交。

## 提交前检查

提交前至少执行：

```powershell
git diff --check
dotnet build Code\RelayServer\RelayServer.sln
```

如果改了前端静态 JS：

```powershell
node --check Code\RelayServer\wwwroot\app.js
```

如果改了固件：

```powershell
.\Code\ESP-IDF\Esp32RemoteAgent\flash-firmware.ps1 `
  -WifiSsid "TEST_SSID" `
  -WifiPassword "TEST_PASSWORD" `
  -ServerHost "192.0.2.1" `
  -BuildOnly
```

如果需要实机验证，已知测试串口为 `COM5`。

## 开发注意事项

- 修改协议时，固件和服务器必须同步修改。
- 修改 USB 描述符后，Windows 可能缓存旧设备，需要卸载旧设备或更换 PID。
- 真实 WiFi 密码、板子密钥、证书、数据库不要提交。
- 固件配置优先通过 `flash-firmware.ps1` 注入临时构建目录。
- 中转服务器页面不要堆复杂视觉效果，应保持面向运维的简洁视图。
- 默认开源协议是 MIT，协议文件位于 [LICENSE](../LICENSE)。
