namespace RelayServer.Models;

public sealed record BoardEditRequest(
    string BoardId,
    string Name,
    string AuthKey,
    int AssignedPort,
    bool Enabled,
    string TargetHost,
    int TargetPort);

public sealed record BoardRegisterRequest(
    string BoardId,
    string AuthKey,
    int AssignedPort,
    string? TargetHost,
    int? TargetPort,
    string? Firmware);
