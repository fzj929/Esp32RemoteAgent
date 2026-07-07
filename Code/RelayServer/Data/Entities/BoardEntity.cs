namespace RelayServer.Data.Entities;

public sealed class BoardEntity
{
    public string BoardId { get; set; } = string.Empty;
    public string Name { get; set; } = string.Empty;
    public string AuthKey { get; set; } = string.Empty;
    public int AssignedPort { get; set; }
    public bool Enabled { get; set; }
    public string? OwnerUsername { get; set; }
    public string TargetHost { get; set; } = string.Empty;
    public int TargetPort { get; set; }
    public DateTimeOffset UpdatedAt { get; set; }
    public UserEntity? Owner { get; set; }
    public List<BoardServiceEntity> Services { get; set; } = [];
}
