namespace TerminalRelayAgent.Models;

public sealed record AgentStatus(
    bool Enabled,
    bool Online,
    string BoardId,
    string Relay,
    DateTimeOffset? ConnectedAt,
    DateTimeOffset? LastHeartbeat,
    int ActiveTunnels,
    long BytesFromServer,
    long BytesFromTarget,
    string? LastError);
