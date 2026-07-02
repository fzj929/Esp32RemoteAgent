namespace RelayServer.Models;

public sealed record RelayEvent(DateTimeOffset Timestamp, string Level, string Message);
