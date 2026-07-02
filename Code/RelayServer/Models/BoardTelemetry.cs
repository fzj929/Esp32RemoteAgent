namespace RelayServer.Models;

public sealed record BoardTelemetry(
    long? UptimeMs,
    long? FreeHeap,
    int? Rssi,
    int? ActiveTunnels,
    long? BytesFromServer,
    long? BytesFromTerminal,
    string? UsbNetif,
    string? Firmware);
