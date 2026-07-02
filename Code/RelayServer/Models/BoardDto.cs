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
    string? RemoteEndPoint)
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
            session?.RemoteEndPoint);
}
