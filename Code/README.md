# Esp32RemoteAgent Code

This directory contains the two deployable parts of the ESP32 remote desktop relay system.

## Projects

- `ESP-IDF/Esp32RemoteAgent`: ESP32-S3 firmware based on ESP-IDF 5.3.x.
- `RelayServer`: .NET 8 relay server and Vue 3 static management UI.

## Network Flow

1. The ESP32-S3 connects to a configured WiFi network.
2. The ESP32-S3 opens a board control tunnel to the relay server on TCP `6555`.
3. The ESP32-S3 exposes a USB virtual network interface to the terminal device.
4. The operations PC connects with Remote Desktop to `relay-server-ip:board-public-port`.
5. The relay server forwards the TCP stream through the ESP32-S3 to the terminal RDP service at `192.168.77.2:3389`.

## Important Notes

- The firmware currently focuses on TCP forwarding for RDP. UDP forwarding can be added later for RDP performance optimization.
- The ESP32-S3 firmware uses TinyUSB network mode. Windows host compatibility depends on the USB network class driver available on the terminal system.
- Do not commit real WiFi passwords, board secrets, generated certificates, SQLite databases, or runtime logs.
