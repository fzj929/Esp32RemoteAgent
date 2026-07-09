# TerminalRelayAgent

`TerminalRelayAgent` 是运行在终端设备上的 .NET 8 TCP 中转代理。它实现和 ESP32 板子相同的中转服务器控制协议，可直接连接中转服务器 `6555` 控制端口，并根据中转服务器后台配置的 TCP 服务规则转发数据。

适用场景：

- 终端设备可以访问中转服务器网络。
- 不需要 ESP32 板子和 USB 虚拟网卡。
- 运维 PC 仍然连接中转服务器公网端口。
- 中转服务器后台仍然使用“板子 ID / 认证密钥 / TCP 服务 / 公网端口”进行配置。

## 工作方式

```text
运维 PC
  -> 中转服务器公网端口
  -> 中转服务器 6555 控制通道
  -> TerminalRelayAgent
  -> 终端本机或局域网 TCP 服务
```

代理启动后会：

1. 读取 `agentsettings.json` 或 `appsettings.json` 中的配置。
2. 使用固定 `BoardId` 和 `BoardKey` 向中转服务器注册。
3. 等待中转服务器下发 `OPEN/DATA/CLOSE` 帧。
4. 对每个连接直接连接目标地址和端口，例如 `127.0.0.1:3389`、`127.0.0.1:80`。
5. 双向转发 TCP 数据。

## 使用前准备

在中转服务器后台新增一块“板子”，这里的板子就是终端代理：

- `BoardId`：和代理配置中的 `BoardId` 一致。
- `AuthKey`：和代理配置中的 `BoardKey` 一致。
- TCP 服务：按需要配置公网端口和目标地址。

如果代理和目标服务在同一台终端上，目标地址通常使用：

```text
127.0.0.1:3389
127.0.0.1:80
127.0.0.1:443
```

## 本地运行

```powershell
cd Code\TerminalRelayAgent
dotnet run
```

默认管理页面：

```text
http://127.0.0.1:19090
```

页面可配置：

- 是否启用代理
- 固定 ID / BoardId
- 认证密钥 / BoardKey
- 中转服务器地址和 6555 控制端口
- 默认目标地址和端口

保存配置后，代理会自动断开当前中转连接并使用新配置重连。

## Windows 发布和启动

```powershell
cd Code\TerminalRelayAgent
.\publish-start.ps1 -WebPort 19090
```

发布目录：

```text
Code\TerminalRelayAgent\publish
```

停止代理：

```powershell
.\stop-agent.ps1
```

只发布不启动：

```powershell
.\publish-start.ps1 -NoStart
```

生成自包含发布包：

```powershell
.\publish-start.ps1 -SelfContained
```

## 注意

- 该程序只转发 TCP，不支持 UDP。
- 该程序不提供 USB 虚拟网卡，不适合终端无法访问中转服务器的现场。
- 中转服务器不需要修改协议即可识别该代理，因为它使用和 ESP32 固件相同的注册与帧协议。
- 生产环境请修改默认 `BoardKey`，不要使用 `CHANGE_THIS_DEVICE_SECRET`。
