# AI 后续开发说明

本文档面向后续接手本项目的 AI 或开发者。目标是减少重复试错，尤其是 ESP-IDF 在 Windows 中文路径、Python 环境、串口烧录上的问题。

## 项目目标

本项目用于让无法直接联网的 Windows 终端设备接受远程桌面访问。

整体链路：

```text
运维 PC
  |
  | RDP: relay-server-ip:6500-6600
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

当前已经实现 TCP RDP 转发。RDP UDP 转发尚未实现，后续可作为性能优化方向。

## 目录结构

```text
README.md
docs/AI_DEVELOPMENT_GUIDE.md
Code/
  ESP-IDF/
    Esp32RemoteAgent/
      flash-firmware.ps1
      sdkconfig.defaults
      main/main.c
      main/Kconfig.projbuild
      main/idf_component.yml
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
- 固件烧录脚本：[flash-firmware.ps1](../Code/ESP-IDF/Esp32RemoteAgent/flash-firmware.ps1)
- 中转服务器入口：[Program.cs](../Code/RelayServer/Program.cs)
- 转发核心：[Relay](../Code/RelayServer/Relay)
- 管理页面：[wwwroot](../Code/RelayServer/wwwroot)

## 关键协议

ESP32-S3 和中转服务器之间保持一条 TCP 控制连接，默认端口 `6555`。

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
| 2 | RegisterAck | 服务器 -> 板子 | 注册成功 |
| 3 | Heartbeat | 板子 -> 服务器 | 心跳 |
| 4 | Open | 服务器 -> 板子 | 打开终端 TCP 连接 |
| 5 | Data | 双向 | 转发 TCP 数据 |
| 6 | Close | 双向 | 关闭连接 |
| 7 | Error | 板子 -> 服务器 | 板子侧错误 |

协议修改必须同时改固件和服务器：

- 固件：`send_frame`、`process_relay_frame`、`handle_data`
- 服务器：`RelayFrame.cs`、`RelayFrameType.cs`、`BoardSession.cs`

## 固件实现要点

入口函数：`app_main`

启动顺序：

1. 初始化 NVS。
2. 初始化 RGB 状态灯。
3. 初始化 tunnel 数组。
4. 启动 WiFi。
5. 启动 USB NCM 虚拟网卡。
6. 在 Core 1 上启动 `relay_task`。

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

仓库中的 `sdkconfig.defaults` 必须保持占位值，不要提交真实 WiFi 密码。

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
- Windows 是否缓存了旧 PID/描述符。
- 是否需要改成完整 RNDIS 描述符。

## 性能优化现状

已做的低风险优化：

- 固件 `MAX_FRAME_PAYLOAD` 调整为 `8192`。
- 固件终端连接和服务器连接设置 `TCP_NODELAY`。
- 固件关闭 WiFi 省电：`esp_wifi_set_ps(WIFI_PS_NONE)`。
- 服务器 `RelayFrame.WriteAsync` 移除每帧 `FlushAsync()`。
- 固件心跳上报 RSSI、free heap、active tunnels、上行/下行字节数和固件版本。
- 服务器会话统计每块板子的公网流入字节、板子返回字节、最后错误和最近心跳。

## 当前已完成的 P0-P3 优化

P0：

- 固件增加 NVS 配置命名空间 `remote_cfg`。
- 首次启动会将编译期配置写入 NVS，后续启动优先读取 NVS。
- 注册报文支持 HMAC-SHA256 签名，不再要求新固件把 `authKey` 明文发给服务器。
- 服务器仍兼容旧固件的明文 `authKey` 注册，方便滚动升级。
- 管理后台拒绝保存默认占位密钥 `CHANGE_THIS_DEVICE_SECRET`。
- 板子的公网远程端口不再固定在固件里；固件注册时发送 `assignedPort=0`，服务器从数据库读取该板子的 `AssignedPort`，并通过 `RegisterAck.assignedPort` 下发。

P1：

- 固件上报更多运行状态。
- 服务器统计每块板子的转发字节数和诊断字段。
- `flash-firmware.ps1` 增加 `-BuildOnly`，后续 AI 可以只编译不烧录。

P2：

- 管理后台增加诊断区域，展示流量、RSSI、heap、最后心跳和固件版本。
- 新增 `/api/boards/diagnostics` 诊断接口。
- 新增 `/api/boards/{boardId}/probe-target` 服务器侧目标端口测试接口。
- 事件日志写入 SQLite `relay_events` 表，服务重启后仍可查询最近事件。

P3：

- 管理员登录增加基于 IP + 用户名的失败限速。
- Cookie 已启用 `HttpOnly` 和 `SameSite=Strict`。
- 发布脚本已有 HTTPS 自签证书流程，生产环境仍建议换成正式证书。

尚未完成、不要误判为已完成：

- RDP UDP 转发尚未实现。
- 板子控制通道 TLS 尚未实现；当前是 HMAC 注册认证 + 裸 TCP 数据通道。
- 现场无需重刷固件的交互式配置工具尚未实现；当前 NVS 可持久化，但主要仍通过烧录脚本注入初始值。

后续性能方向：

- 拆分板端收发任务，减少单任务轮询延迟。
- 增加 UDP 3389 转发。
- 管理后台显示上下行速率、延迟、最后错误。
- 增加按板子的吞吐统计和连接质量诊断。

## 固件编译和烧录标准流程

不要直接在本仓库中文路径下运行 `idf.py build`。ESP-IDF 在 Windows 下可能把中文路径转码成乱码，导致 Kconfig 找不到文件。

请使用脚本：

```powershell
.\Code\ESP-IDF\Esp32RemoteAgent\flash-firmware.ps1 `
  -WifiSsid "YOUR_WIFI_SSID" `
  -WifiPassword "YOUR_WIFI_PASSWORD" `
  -ServerHost "YOUR_RELAY_SERVER_IP" `
  -Port "COM5" `
  -BoardId "S3-0001" `
  -BoardKey "CHANGE_THIS_DEVICE_SECRET" `
  -AssignedPublicPort 6500
```

脚本职责：

1. 将固件工程复制到 `C:\tmp\Esp32RemoteAgentBuild`。
2. 只在临时目录修改 `sdkconfig.defaults`，写入 WiFi 和服务器配置。
3. 设置 `PROCESSOR_ARCHITECTURE=AMD64`，解决自动化环境下 ESP-IDF 识别平台为 `Windows-` 的问题。
4. 设置 `IDF_TOOLS_PATH=C:\Espressif`。
5. 使用 `C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe` 调用 `idf.py`。
6. 执行 `idf.py -p COMx build flash`。

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
```

注意：

- 不要把真实 WiFi 密码写入仓库文件。
- 不要提交 `sdkconfig`、`dependencies.lock`、`managed_components`、`build`。
- 如果换机器，先确认 ESP-IDF 安装路径和 Python env 路径。

## 中转服务器实现要点

服务器使用 .NET 8、SQLite、静态 Vue 3 页面。

主要模块：

- `Program.cs`：应用启动、服务注册、API 映射。
- `Relay/ControlChannelService.cs`：监听板子控制端口 `6555`。
- `Relay/BoardSession.cs`：每块板子的在线会话和公网端口监听。
- `Relay/RelayFrame.cs`：板子和服务器之间的帧读写。
- `Relay/RelayHub.cs`：在线板子和事件列表。
- `Data/BoardRepository.cs`：板子配置 SQLite 存储。
- `Data/AuthRepository.cs`：管理员账号存储。
- `wwwroot/`：管理页面。

服务器开发验证：

```powershell
dotnet build Code\RelayServer\RelayServer.sln
```

发布启动：

```powershell
cd Code\RelayServer
.\publish-start.ps1
```

运行产物如 `publish`、`certs`、`relay.db`、日志、PID 文件均不应提交。

## 开发注意事项

- 修改协议时，固件和服务器必须同步修改。
- 修改 USB 描述符后，Windows 可能缓存旧设备，需要卸载旧设备或更换 PID。
- 真实 WiFi 密码、板子密钥、证书、数据库不要提交。
- 固件配置优先通过 `flash-firmware.ps1` 注入临时构建目录。
- 中转服务器提交前至少运行 `dotnet build Code\RelayServer\RelayServer.sln`。
- 固件提交前尽量运行 `flash-firmware.ps1` 或在 ASCII 路径下运行 `idf.py build`。

## 当前已知限制

- 当前只实现 TCP RDP 转发，未实现 UDP。
- USB 网络当前以 NCM 为主，部分 Windows 镜像可能需要 RNDIS。
- 板子认证当前是静态密钥，后续可改为 HMAC challenge-response 或 TLS。
- 固件配置目前主要编译期注入，后续可加入 NVS 落地配置和 Web/BLE 配网。
