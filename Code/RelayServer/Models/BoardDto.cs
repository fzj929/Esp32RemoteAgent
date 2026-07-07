using RelayServer.Relay;

namespace RelayServer.Models;

public sealed record BoardDto(
    string BoardId,
    string Name,
    int AssignedPort,
    bool Enabled,
    string? OwnerUsername,
    string TargetHost,
    int TargetPort,
    IReadOnlyList<BoardServiceDto> Services,
    DateTimeOffset UpdatedAt,
    bool Online,
    int ActiveConnections,
    DateTimeOffset? ConnectedAt,
    DateTimeOffset? LastHeartbeat,
    string? RemoteEndPoint,
    string? Firmware,
    long BytesFromPublic,
    long BytesFromBoard,
    string? LastError,
    BoardTelemetry? Telemetry)
{
    public static BoardDto From(BoardRecord board, BoardSession? session) =>
        new(
            board.BoardId,
            board.Name,
            board.AssignedPort,
            board.Enabled,
            board.OwnerUsername,
            board.TargetHost,
            board.TargetPort,
            board.Services.Select(BoardServiceDto.From).ToList(),
            board.UpdatedAt,
            session is not null,
            session?.ActiveConnectionCount ?? 0,
            session?.ConnectedAt,
            session?.LastHeartbeat,
            session?.RemoteEndPoint,
            session?.Firmware,
            session?.BytesFromPublic ?? 0,
            session?.BytesFromBoard ?? 0,
            session?.LastError,
            session?.Telemetry);
}

public sealed record BoardServiceDto(
    string Name,
    int PublicPort,
    string TargetHost,
    int TargetPort,
    bool Enabled)
{
    public string Target => $"{TargetHost}:{TargetPort}";

    public static BoardServiceDto From(BoardServiceRecord service) =>
        new(service.Name, service.PublicPort, service.TargetHost, service.TargetPort, service.Enabled);
}
