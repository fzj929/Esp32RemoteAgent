namespace RelayServer.Models;

public sealed record BoardRecord(
    string BoardId,
    string Name,
    string AuthKey,
    int AssignedPort,
    bool Enabled,
    string? OwnerUsername,
    string TargetHost,
    int TargetPort,
    DateTimeOffset UpdatedAt,
    IReadOnlyList<BoardServiceRecord> Services);
