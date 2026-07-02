# 压缩上下文

本项目已经跑通：运维 PC 通过中转服务器公网端口远程桌面到现场终端设备。

当前核心链路：

```text
运维 PC -> RelayServer 公网端口 6500-6600 -> ESP32-S3 TCP 控制通道 6555 -> USB NCM -> 终端 192.168.77.2:3389
```

最新实现状态：

- 中转服务器：.NET 8、SQLite、静态 Vue3 管理页面。
- 固件：ESP-IDF 5.3.x，ESP32-S3，TinyUSB NCM，RGB 状态灯。
- 编译烧录：使用 `Code/ESP-IDF/Esp32RemoteAgent/flash-firmware.ps1`，脚本会复制到 `C:\tmp\Esp32RemoteAgentBuild` 避免中文路径问题。
- 已验证烧录端口：`COM5`。

最近一轮优化：

- 固件增加 NVS 配置读取，首次启动写入默认配置。
- 固件注册改为 HMAC-SHA256 签名，避免新固件明文发送板子密钥。
- 固件心跳上报 RSSI、free heap、active tunnels、流量计数、固件版本。
- 服务器兼容 HMAC 注册和旧版 authKey 注册。
- 服务器增加每块板子的流量、心跳、固件版本、最后错误等诊断字段。
- 管理后台增加诊断面板、目标端口测试、登录失败限速。
- 事件日志持久化到 SQLite。

提交前必须验证：

```powershell
dotnet build Code\RelayServer\RelayServer.sln
node --check Code\RelayServer\wwwroot\app.js
Code\ESP-IDF\Esp32RemoteAgent\flash-firmware.ps1 `
  -WifiSsid TEST_SSID `
  -WifiPassword TEST_PASSWORD `
  -ServerHost 192.0.2.1 `
  -BuildOnly
```

不要提交：

- 真实 WiFi 密码
- 真实板子密钥
- `sdkconfig`
- `dependencies.lock`
- `managed_components`
- `build`
- SQLite 数据库
- 证书、日志、PID 文件

仍未完成：

- RDP UDP 转发。
- 控制通道 TLS。
- 现场交互式配置工具，例如串口命令、BLE、AP 配网或 Web 配置页。
- OTA 升级。
