# ESP32-S3 Remote Agent Firmware

ESP-IDF firmware for an ESP32-S3 board used as a field-side RDP relay agent.

## Features

- Connects to a fixed WiFi network.
- Registers to the relay server control channel on TCP `6555`.
- Receives public RDP tunnel requests from the relay server.
- Forwards tunnel traffic to the terminal device at `192.168.77.2:3389`.
- Exposes a USB virtual network interface for the terminal-side link.
- Uses a board ID, authentication key, and assigned public port.
- Controls the onboard RGB LED:
  - Red: relay server disconnected.
  - Green: relay server connected and registered.
  - Blue flash: tunnel data is being forwarded.

## Default USB Network

- ESP32-S3 USB-side IP: `192.168.77.1/24`
- Terminal device IP: DHCP, or manually set `192.168.77.2/24`
- Terminal RDP target: `192.168.77.2:3389`

## Factory Configuration

Defaults are stored in `sdkconfig.defaults` and can also be changed with:

```powershell
idf.py menuconfig
```

Open `Remote Agent Configuration` and set:

- WiFi SSID and password
- Relay server IPv4 address
- Board ID
- Board authentication key
- Assigned public port, usually `6500-6600`
- Terminal RDP host and port

Never commit real production WiFi passwords or board authentication keys.

## Build

Use an ESP-IDF 5.3.x shell:

```powershell
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
```

If the project path contains non-ASCII characters and CMake/toolchain detection fails on Windows, build from a temporary ASCII-only path such as `C:\tmp\Esp32RemoteAgentFlash`.

## Flash and Monitor

```powershell
idf.py -p COM5 flash monitor
```

Replace `COM5` with the actual ESP32-S3 serial port.

## Windows USB Network Notes

The firmware uses TinyUSB network support and provides descriptors intended to help Windows bind the USB network driver. If Windows still does not create a network adapter:

- Connect to the ESP32-S3 native USB OTG port, not only the UART download port.
- Use a USB cable that supports data.
- Remove stale/unknown ESP32 USB devices from Device Manager, then reconnect.
- Check whether Windows shows an unknown USB device, a NCM device, or a driver binding error.
- If NCM is not accepted by the target Windows image, implement and switch to a full RNDIS descriptor.
