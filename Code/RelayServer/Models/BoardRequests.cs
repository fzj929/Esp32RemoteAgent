namespace RelayServer.Models;

public sealed record BoardEditRequest(
    string BoardId,
    string Name,
    string AuthKey,
    int AssignedPort,
    bool Enabled,
    string? OwnerUsername,
    string TargetHost,
    int TargetPort,
    IReadOnlyList<BoardServiceEditRequest>? Services);

public sealed record BoardServiceEditRequest(
    string Name,
    int PublicPort,
    string TargetHost,
    int TargetPort,
    bool Enabled);

public sealed record BoardRegisterRequest(
    string BoardId,
    string? AuthKey,
    int AssignedPort,
    string? TargetHost,
    int? TargetPort,
    string? Firmware,
    string? AuthNonce,
    long? AuthTimestampMs,
    string? AuthSignature);
