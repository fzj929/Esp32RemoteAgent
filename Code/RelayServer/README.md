# ESP32 Remote Relay Server

.NET 8 relay server for ESP32-S3 remote desktop access.

## Features

- Board control channel on TCP `6555`.
- Public RDP entry ports, normally `6500-6600`.
- One public port maps to one registered ESP32 board.
- TCP tunnel relay from operations PC to terminal RDP service.
- SQLite persistence for boards, admin account, and recent events.
- Cookie-based administrator login.
- Static Vue 3 management UI served from `wwwroot`.
- HTTPS support through the publish/start script.

## Roles

- Terminal device: cannot access the Internet directly; exposes Remote Desktop on TCP `3389`.
- ESP32-S3 board: connects to WiFi, registers to the relay server, and forwards RDP tunnel traffic over USB network to the terminal.
- Relay server: owns the public IP and forwards operations PC connections to the correct ESP32 board.
- Operations PC: connects with Remote Desktop to `relay-server-ip:board-public-port`.

## Development Run

```powershell
dotnet restore
dotnet run --project .\RelayServer.csproj
```

Default management URLs:

```text
http://server-ip:18080
https://server-ip:18443
```

## Publish and Start

Run from this directory:

```powershell
.\publish-start.ps1
```

The script:

- Publishes the .NET 8 server to `publish`.
- Copies the static Vue 3 management UI.
- Generates a self-signed HTTPS certificate under `certs`.
- Generates a random bootstrap admin password under `publish`.
- Starts the relay server and writes a PID file.

Custom ports:

```powershell
.\publish-start.ps1 -HttpPort 18080 -HttpsPort 18443 -ControlPort 6555
```

Stop a server started by the script:

```powershell
.\stop-server.ps1
```

## Administrator Login

Development default:

```text
username: admin
password: admin123456
```

Published deployments use a generated password stored in:

```text
publish\admin.bootstrap.password.txt
```

Change the password immediately after first login.

## Board Registration

Add each board in the management page before the board connects:

- Board ID
- Board authentication key
- Public port, usually `6500-6600`
- Terminal target, usually `192.168.77.2:3389`

The control port `6555` is reserved and must not be assigned as a board public port.

## Remote Desktop

Operations PC connects to:

```text
relay-server-ip:board-public-port
```

The current relay implementation forwards TCP. UDP forwarding for RDP can be added separately if required.
