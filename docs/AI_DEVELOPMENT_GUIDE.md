# AI Development Guide

本文档面向后续 AI 或开发者接手本项目时使用，目标是快速理解当前实现方式、关键设计取舍、代码入口和后续开发方向。

## 1. 项目目标

本项目用于实现“无法直接联网的 Windows 终端设备”的远程桌面访问。

现场部署一个 ESP32-S3 板子：

- ESP32-S3 连接现场 WiFi。
- ESP32-S3 主动连接公网中转服务器。
- ESP32-S3 通过 USB 连接终端设备，并模拟 USB 虚拟网卡。
- 运维 PC 通过远程桌面连接中转服务器的指定端口。
- 中转服务器把 RDP TCP 数据转发给对应板子。
- 板子再把数据转发给终端设备的 `3389` 端口。

当前实现重点是 TCP RDP 转发。RDP UDP 转发尚未实现。

## 2. 目录结构

```text
README.md
Code/
  README.md
  ESP-IDF/
    Esp32RemoteAgent/
      main/main.c
      main/Kconfig.projbuild
      sdkconfig.defaults
      README.md
  RelayServer/
    Program.cs
    Relay/
    Data/
    Endpoints/
    Models/
    wwwroot/
    publish-start.ps1
    README.md
```

核心代码：

- 固件：`Code/ESP-IDF/Esp32RemoteAgent/main/main.c`
- 服务端入口：`Code/RelayServer/Program.cs`
- 服务端转发核心：`Code/RelayServer/Relay/`
- 管理后台静态页面：`Code/RelayServer/wwwroot/`

## 3. 系统角色

### 终端设备

现场 Windows 设备，不能直接联网，只需要开启远程桌面服务。

默认网络配置：

- USB 网卡 IP：`192.168.77.2/24`
- RDP 服务：TCP `3389`

如果 DHCP 没有成功，也可以手动配置这个 IP。

### ESP32-S3 板子

板子负责两个方向：

- WiFi 方向：连接中转服务器。
- USB 方向：模拟 USB 虚拟网卡，访问终端设备。

默认配置：

- 板子 USB 侧 IP：`192.168.77.1/24`
- 终端目标：`192.168.77.2:3389`
- 中转服务器控制端口：`6555`
- 板子公网入口端口：`6500-6600` 中的一个。

### 中转服务器

.NET 8 WebAPI 服务，负责：

- 管理板子信息。
- 接收板子控制连接。
- 打开公网 RDP 入口端口。
- 把运维 PC 的 TCP 流转发到对应板子。
- 提供管理后台页面。

### 运维 PC

运维人员使用 Windows 远程桌面连接：

```text
relay-server-ip:assigned-public-port
```

例如：

```text
192.168.0.87:6500
```

## 4. 通信协议

板子和中转服务器之间使用一个常驻 TCP 控制连接，默认端口 `6555`。

每个协议帧格式固定：

```text
byte 0      : frame type
byte 1-4    : connection id, uint32 big-endian
byte 5-8    : payload length, uint32 big-endian
byte 9...   : payload bytes
```

帧类型：

| Type | 名称 | 方向 | 说明 |
|---:|---|---|---|
| 1 | Register | 板子 -> 服务器 | 板子注册 |
| 2 | RegisterAck | 服务器 -> 板子 | 注册成功 |
| 3 | Heartbeat | 板子 -> 服务器 | 心跳 |
| 4 | Open | 服务器 -> 板子 | 打开一个终端 TCP 连接 |
| 5 | Data | 双向 | 转发 TCP 数据 |
| 6 | Close | 双向 | 关闭连接 |
| 7 | Error | 板子 -> 服务器 | 板子侧错误 |

服务端实现位置：

- `Code/RelayServer/Relay/RelayFrame.cs`
- `Code/RelayServer/Relay/RelayFrameType.cs`
- `Code/RelayServer/Relay/BoardSession.cs`

固件实现位置：

- `send_frame`
- `read_exact`
- `register_board`
- `process_relay_frame`
- `pump_terminal_traffic`

这些函数都在 `Code/ESP-IDF/Esp32RemoteAgent/main/main.c`。

## 5. 板子固件实现思路

### 启动流程

入口函数：`app_main`

当前顺序：

1. 初始化 NVS。
2. 初始化 RGB 状态灯。
3. 初始化 tunnel 数组。
4. 启动 WiFi。
5. 启动 USB 虚拟网卡。
6. 在 Core 1 上启动 `relay_task`。

WiFi 和 TinyUSB 主要运行在系统任务/Core 0；业务转发任务固定在 Core 1。

### WiFi

入口：`start_wifi`

配置来自 `sdkconfig.defaults` 或 `idf.py menuconfig`：

- `CONFIG_REMOTE_AGENT_WIFI_SSID`
- `CONFIG_REMOTE_AGENT_WIFI_PASSWORD`
- `CONFIG_REMOTE_AGENT_SERVER_HOST`
- `CONFIG_REMOTE_AGENT_SERVER_CONTROL_PORT`

注意：仓库中默认值必须使用占位符，不要提交真实 WiFi 密码。

### USB 虚拟网卡

入口：`startUsbNetwork`

当前使用 ESP-IDF managed component：

- `espressif/esp_tinyusb`

当前模式：

- `CONFIG_TINYUSB_NET_MODE_NCM=y`

实现方式：

- `tinyusb_driver_install` 安装 TinyUSB device。
- `tinyusb_net_init` 初始化 TinyUSB 网络类。
- `esp_netif_new` 创建 USB 侧 lwIP netif。
- `ESP_NETIF_DHCP_SERVER` 开启 DHCP server。
- `usb_recv_callback` 把 USB 收到的以太网帧送入 `esp_netif_receive`。
- `usb_netif_transmit` 把 lwIP 输出帧送给 `tinyusb_net_send_sync`。

当前已增加：

- Microsoft OS 2.0 BOS 描述符，尝试让 Windows 自动绑定 `WINNCM`。
- 自定义 USB Device Descriptor，使用新 PID 避免 Windows 缓存旧设备描述符。

已知风险：

- 部分 Windows 镜像不会自动接受 NCM。
- 如果终端 Windows 仍不出现虚拟网卡，下一步应实现完整 RNDIS configuration descriptor，或改用 CDC-ECM/RNDIS 模式。

### TCP 转发

入口：`relay_task`

逻辑：

1. 等待 WiFi 获取 IP。
2. TCP 连接中转服务器 `SERVER_HOST:SERVER_CONTROL_PORT`。
3. 发送 Register 帧。
4. 等待 RegisterAck。
5. 循环处理：
   - 服务器下发 Open/Data/Close。
   - 板子连接终端 `192.168.77.2:3389`。
   - 板子把终端返回数据封装为 Data 帧发给服务器。
   - 定时发送 Heartbeat。

每个 RDP 连接在固件中对应一个 `tunnel_connection_t`：

```c
typedef struct {
    uint32_t id;
    int fd;
    bool active;
    int64_t last_activity_ms;
} tunnel_connection_t;
```

当前最大并发连接数：

```c
MAX_TUNNELS = 4
```

### RGB 状态灯

使用 `espressif/led_strip` 组件，默认 GPIO `48`。

状态：

- 红色：未连接/未注册中转服务器。
- 绿色：已连接并注册成功。
- 蓝色短闪：有数据转发。

相关函数：

- `status_led_init`
- `status_led_set_disconnected`
- `status_led_set_connected`
- `status_led_note_data`
- `status_led_tick`

## 6. 中转服务器实现思路

### 启动入口

文件：

```text
Code/RelayServer/Program.cs
```

主要职责：

- 加载配置。
- 初始化 SQLite。
- 初始化管理员账号。
- 注册后台服务。
- 映射 API endpoints。
- 托管 `wwwroot` 静态管理页面。

### 数据存储

使用 SQLite。

主要仓储：

- `Data/AuthRepository.cs`
- `Data/BoardRepository.cs`

默认数据库路径：

```json
"DatabasePath": "relay.db"
```

数据库文件是运行时文件，不应提交。

### 管理后台

静态 Vue 3 页面：

- `wwwroot/index.html`
- `wwwroot/app.js`
- `wwwroot/styles.css`

功能：

- 管理员登录。
- 修改密码。
- 添加/编辑/删除板子。
- 查看事件。
- 查看在线状态。

### 板子控制通道

核心服务：

```text
Relay/ControlChannelService.cs
Relay/BoardSession.cs
Relay/RelayHub.cs
```

`ControlChannelService` 监听 TCP `6555`。

板子连接后发送 Register：

```json
{
  "boardId": "S3-0001",
  "authKey": "...",
  "assignedPort": 6500,
  "targetHost": "192.168.77.2",
  "targetPort": 3389,
  "firmware": "esp-idf-s3-0.1.0"
}
```

服务端校验：

- boardId 是否存在。
- authKey 是否匹配。
- assignedPort 是否匹配后台配置。
- 端口是否在允许范围内。

注册成功后，服务端为该板打开公网监听端口。

### 公网 RDP 入口

每块板对应一个 public port。

运维 PC 连接 public port 后：

1. 服务端分配 connection id。
2. 给板子发送 Open 帧。
3. 后续 TCP 数据封装为 Data 帧发给板子。
4. 板子连接终端 RDP 端口，并双向转发。

## 7. 发布脚本

文件：

```text
Code/RelayServer/publish-start.ps1
```

职责：

- `dotnet publish`
- 生成自签 HTTPS 证书。
- 生成随机管理员初始密码。
- 写入生产配置。
- 启动服务。

生成文件：

- `publish/`
- `certs/`
- `publish/admin.bootstrap.password.txt`
- `publish/relay-server.pid`

这些都是运行时/敏感文件，已由 `.gitignore` 排除。

## 8. 配置项说明

固件配置在：

```text
Code/ESP-IDF/Esp32RemoteAgent/sdkconfig.defaults
Code/ESP-IDF/Esp32RemoteAgent/main/Kconfig.projbuild
```

关键配置：

| 配置 | 说明 |
|---|---|
| `CONFIG_REMOTE_AGENT_WIFI_SSID` | WiFi 名称 |
| `CONFIG_REMOTE_AGENT_WIFI_PASSWORD` | WiFi 密码 |
| `CONFIG_REMOTE_AGENT_SERVER_HOST` | 中转服务器 IPv4 |
| `CONFIG_REMOTE_AGENT_SERVER_CONTROL_PORT` | 控制端口，默认 `6555` |
| `CONFIG_REMOTE_AGENT_BOARD_ID` | 板子 ID |
| `CONFIG_REMOTE_AGENT_BOARD_KEY` | 板子认证密钥 |
| `CONFIG_REMOTE_AGENT_ASSIGNED_PUBLIC_PORT` | 分配给板子的公网端口 |
| `CONFIG_REMOTE_AGENT_TERMINAL_RDP_HOST` | 终端 RDP IP |
| `CONFIG_REMOTE_AGENT_TERMINAL_RDP_PORT` | 终端 RDP 端口 |
| `CONFIG_REMOTE_AGENT_STATUS_LED_GPIO` | RGB LED GPIO |

服务端配置在：

```text
Code/RelayServer/appsettings.json
```

关键配置：

| 配置 | 说明 |
|---|---|
| `Relay.ControlPort` | 板子控制通道端口 |
| `Relay.PublicPortMin` | 公网端口范围起始 |
| `Relay.PublicPortMax` | 公网端口范围结束 |
| `Relay.ReservedPorts` | 保留端口 |
| `Relay.DefaultTargetHost` | 默认终端 IP |
| `Relay.DefaultTargetPort` | 默认终端端口 |
| `Relay.HeartbeatTimeoutSeconds` | 心跳超时 |
| `Relay.DatabasePath` | SQLite 文件路径 |

## 9. 当前已知问题

### Windows 终端可能不出现 USB 虚拟网卡

当前固件实现 NCM，并尝试通过 Microsoft OS 2.0 描述符绑定 `WINNCM`。

如果 Windows 仍未出现网卡，优先排查：

- 是否连接 ESP32-S3 原生 USB OTG 口。
- USB 线是否支持数据。
- 设备管理器是否出现未知 USB 设备。
- Windows 是否缓存过旧 PID/描述符。
- 是否需要卸载旧设备后重新插拔。

如果仍失败，后续开发建议：

1. 实现完整 RNDIS descriptor。
2. 切换 `CONFIG_TINYUSB_NET_MODE_ECM_RNDIS=y`。
3. 提供 RNDIS + ECM 双配置，Windows 走 RNDIS，Linux/macOS 走 ECM。

### UDP 尚未实现

RDP 可以只走 TCP，但 UDP 能改善体验。

如需实现 UDP，需要新增：

- 服务端 UDP public port 监听。
- 板子和服务端之间的 UDP 数据帧类型或独立 UDP tunnel。
- 连接生命周期映射和 NAT 超时处理。

### 认证强度

当前 board auth key 是静态密钥。后续可增强：

- HMAC challenge-response。
- 固件证书。
- TLS 控制通道。
- 每板唯一 token 轮换。

## 10. 后续开发建议

优先级建议：

1. 解决 Windows USB 虚拟网卡兼容性，必要时实现 RNDIS。
2. 增加端到端诊断页面：板子在线、最近心跳、最后错误、USB link 状态。
3. 增加固件 NVS 配置落地，让现场工程师不用重新编译即可改 WiFi/服务器。
4. 增加 OTA 升级能力。
5. 增加 UDP 转发。
6. 增强认证和传输加密。
7. 增加集成测试和协议兼容性测试。

## 11. AI 接手开发提示

后续 AI 修改本项目时建议遵守：

- 不要提交真实 WiFi 密码、板子密钥、证书、数据库和日志。
- 修改协议时必须同步改固件和服务端。
- 修改 USB 描述符后必须换 PID 或清理 Windows 设备缓存，否则测试结果可能不可信。
- 修改固件配置后执行 `idf.py reconfigure`。
- Windows 下如果工程路径包含中文导致 ESP-IDF 构建失败，可复制到 `C:\tmp\Esp32RemoteAgentFlash` 这种 ASCII 路径构建。
- 服务端提交前至少运行 `dotnet build Code\RelayServer\RelayServer.sln`。
- 固件提交前至少在 ESP-IDF 环境下运行 `idf.py build`。

