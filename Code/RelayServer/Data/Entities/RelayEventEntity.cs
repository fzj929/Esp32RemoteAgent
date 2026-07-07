namespace RelayServer.Data.Entities;

public sealed class RelayEventEntity
{
    public long Id { get; set; }
    public DateTimeOffset Timestamp { get; set; }
    public string Level { get; set; } = string.Empty;
    public string Message { get; set; } = string.Empty;
}
