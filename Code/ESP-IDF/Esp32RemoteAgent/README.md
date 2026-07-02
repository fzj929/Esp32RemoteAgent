# ESP32-S3 Remote Agent Firmware

ESP-IDF firmware for an ESP32-S3 board used as a field-side RDP relay agent.

## Features

- Connects to a fixed WiFi network.
- Registers to the relay server control channel on TCP `6555`.
- Receives public RDP tunnel requests from the relay server.
- Forwards tunnel traffic to the terminal device at `192.168.77.2:3389`.
- Exposes a USB virtual network interface for the terminal-side link.
- Uses a board ID, authentication key, and assigned public port.
- Persists startup configuration in NVS after first boot.
- Registers with HMAC-SHA256 in current firmware so the board key is not sent in plaintext.
- Sends heartbeat telemetry including RSSI, free heap, active tunnel count, traffic counters, and firmware version.
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

## Recommended Flash Script

Use `flash-firmware.ps1` from the repository root or from any PowerShell directory:

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

The script was added to make repeated flashing reliable on Windows. It:

- Copies this firmware project to `C:\tmp\Esp32RemoteAgentBuild`.
- Rewrites `sdkconfig.defaults` only in the temporary copy.
- Sets `PROCESSOR_ARCHITECTURE=AMD64` when the automation shell does not provide it.
- Sets `IDF_TOOLS_PATH=C:\Espressif`.
- Uses the tested ESP-IDF Python environment at `C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe`.
- Runs `idf.py -p COMx build flash`.

This avoids two common Windows problems:

- ESP-IDF Kconfig failures when the source path contains Chinese or other non-ASCII characters.
- ESP-IDF tool discovery failures when the shell does not expose the expected processor architecture environment variable.

Optional flags:

```powershell
-BuildOnly    # compile without flashing
-Monitor      # open idf.py monitor after flashing
-EraseFlash   # erase flash before build/flash
```

NVS note: the firmware saves compile-time defaults into the `remote_cfg` namespace on first boot and then reads from NVS on later boots. Use `-EraseFlash` when you intentionally want to reset stored board configuration.

Manual flash is still possible from an ESP-IDF shell when the project path is ASCII-only:

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
