namespace RelayServer.Data.Entities;

public sealed class PublicPortAllocationEntity
{
    public int PublicPort { get; set; }
    public string CustomerName { get; set; } = string.Empty;
    public string? Note { get; set; }
    public bool Enabled { get; set; } = true;
    public DateTimeOffset UpdatedAt { get; set; } = DateTimeOffset.UtcNow;
}
