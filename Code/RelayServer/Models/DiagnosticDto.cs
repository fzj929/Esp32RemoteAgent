namespace RelayServer.Models;

public sealed record RelayDiagnosticDto(
    int BoardCount,
    int OnlineBoards,
    int ActiveConnections,
    long BytesFromPublic,
    long BytesFromBoard,
    DateTimeOffset ServerTime,
    IReadOnlyList<BoardDto> Boards);

public sealed record TargetProbeDto(
    string BoardId,
    string Target,
    bool Success,
    long ElapsedMs,
    string? Error);
