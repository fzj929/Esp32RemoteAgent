# ESP32 Remote Relay Server

.NET 8 relay server for ESP32-S3 TCP remote access.

## Features

- Board control channel on TCP `6555`.
- Public TCP entry ports, normally `6500-6600`.
- One board can expose multiple TCP services, each with its own public port.
- TCP tunnel relay from operations PC to terminal services such as RDP, HTTP, HTTPS, SSH, Modbus TCP, or custom TCP protocols.
- EF Core data layer with SQLite by default and MySQL support for production deployments.
- Role-based access control: administrators can see every board, normal users only see boards assigned to their username.
- Cookie-based administrator login.
- Static Vue 3 management UI served from `wwwroot`.
- HTTPS support through the publish/start script.

## Roles

- Terminal device: cannot access the Internet directly; exposes one or more TCP services on the USB network.
- ESP32-S3 board: connects to WiFi, registers to the relay server, and forwards TCP tunnel traffic over USB network to the terminal.
- Relay server: owns the public IP and forwards operations PC connections to the correct ESP32 board.
- Operations PC: connects to `relay-server-ip:service-public-port` with the matching client, such as Remote Desktop, browser, SSH client, or other TCP client.

## Development Run

```powershell
dotnet restore
dotnet run --project .\RelayServer.csproj
```

The server uses EF Core. Default database settings:

```json
"Database": {
  "Provider": "Sqlite",
  "ConnectionString": "Data Source=relay.db"
}
```

MySQL example:

```json
"Database": {
  "Provider": "MySql",
  "ConnectionString": "Server=127.0.0.1;Port=3306;Database=esp32_relay;User=relay;Password=YOUR_PASSWORD;",
  "ServerVersion": "8.0.36"
}
```

The current backend schema is a clean EF Core model. Older handwritten SQLite tables are not treated as an upgrade target.

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

- Builds the Vue 3 ClientApp into `wwwroot`.
- Publishes the .NET 8 server to `publish`.
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
- One or more TCP services:
  - Service name, such as `RDP`, `HTTP`, or `HTTPS`
  - Public port, usually `6500-6600`
  - Terminal target, such as `192.168.77.2:3389`, `192.168.77.2:80`, or `192.168.77.2:443`

The control port `6555` is reserved and must not be assigned as a service public port.

## TCP Access

Operations PC connects to the public port configured for a service:

```text
relay-server-ip:service-public-port
```

Examples:

- `relay-server-ip:6500` -> terminal `192.168.77.2:3389` for RDP.
- `http://relay-server-ip:6501` -> terminal `192.168.77.2:80` for HTTP.
- `https://relay-server-ip:6502` -> terminal `192.168.77.2:443` for HTTPS.

HTTPS is forwarded as raw TCP. TLS is not terminated by the relay server or the board, so browser certificate name warnings may still appear if the terminal certificate does not match the public hostname.

The current relay implementation forwards TCP. UDP forwarding for RDP UDP acceleration, QUIC/HTTP3, or DNS UDP can be added separately if required.
