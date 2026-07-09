namespace RelayServer.Models;

public sealed record PublicPortAllocationDto(
    int PublicPort,
    string CustomerName,
    string? Note,
    bool Enabled,
    DateTimeOffset UpdatedAt,
    string? UsedByBoardId,
    string? UsedByServiceName);

public sealed record PublicPortAllocationRequest(
    int PublicPort,
    string CustomerName,
    string? Note,
    bool Enabled);
