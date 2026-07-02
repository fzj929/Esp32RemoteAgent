using RelayServer.Relay;

namespace RelayServer.Models;

public sealed record BoardDto(
    string BoardId,
    string Name,
    int AssignedPort,
    bool Enabled,
    string TargetHost,
    int TargetPort,
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
            board.TargetHost,
            board.TargetPort,
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
