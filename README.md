# Esp32RemoteAgent

ESP32-S3 remote desktop relay system for field terminals that cannot directly access the Internet.

The system uses an ESP32-S3 board as an on-site network bridge and a public relay server as the entry point for operations staff. The operations PC connects to the relay server with Remote Desktop, and the relay server forwards the TCP stream through the ESP32-S3 board to the terminal device's RDP port.

## Architecture

```text
Operations PC
    |
    | RDP to relay-server-ip:6500-6600
    v
Relay Server (.NET 8, public IP)
    |
    | Board control tunnel, TCP 6555
    v
ESP32-S3 Board
    |
    | USB virtual network, 192.168.77.0/24
    v
Terminal Device
    |
    | RDP service
    v
TCP 3389
```

## Components

- `Code/ESP-IDF/Esp32RemoteAgent`
  - ESP-IDF 5.3.x firmware for ESP32-S3.
  - WiFi station mode.
  - TinyUSB virtual network interface for terminal-side USB networking.
  - Relay control tunnel and TCP forwarding.
  - RGB status LED.

- `Code/RelayServer`
  - .NET 8 relay service.
  - SQLite persistence.
  - Vue 3 static management page.
  - Administrator login.
  - HTTPS publish/start script.

## Main Features

- Fixed WiFi credentials in firmware configuration.
- Board registration with board ID, authentication key, and assigned public port.
- Relay server control port: TCP `6555`.
- Board public ports: normally `6500-6600`.
- Terminal RDP target: `192.168.77.2:3389`.
- Operations PC connects with Remote Desktop to `relay-server-ip:assigned-port`.
- Management page for boards and events.
- HTTPS support through generated self-signed certificate.
- RGB status indication:
  - Red: relay disconnected.
  - Green: relay connected and registered.
  - Blue flash: tunnel data activity.

## Security Notes

- Do not commit real WiFi passwords, board keys, generated certificates, SQLite databases, or runtime logs.
- Replace the development admin password before deployment.
- Use trusted certificates for production HTTPS.
- Restrict relay server firewall rules to required ports.

## Quick Start

Build and flash firmware:

```powershell
cd Code\ESP-IDF\Esp32RemoteAgent
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
idf.py -p COM5 flash monitor
```

Run relay server for development:

```powershell
cd Code\RelayServer
dotnet restore
dotnet run --project .\RelayServer.csproj
```

Publish and start relay server with HTTPS:

```powershell
cd Code\RelayServer
.\publish-start.ps1
```

## Current Limitations

- RDP TCP forwarding is implemented. UDP forwarding can be added later for better RDP performance.
- USB virtual network compatibility depends on the terminal Windows USB network driver support. NCM is implemented; some Windows images may require RNDIS.
