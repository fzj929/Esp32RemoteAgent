namespace RelayServer.Models;

public sealed record BoardServiceRecord(
    string BoardId,
    string Name,
    int PublicPort,
    string TargetHost,
    int TargetPort,
    bool Enabled);
