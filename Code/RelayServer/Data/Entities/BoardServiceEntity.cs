namespace RelayServer.Data.Entities;

public sealed class BoardServiceEntity
{
    public string BoardId { get; set; } = string.Empty;
    public string Name { get; set; } = string.Empty;
    public int PublicPort { get; set; }
    public string TargetHost { get; set; } = string.Empty;
    public int TargetPort { get; set; }
    public bool Enabled { get; set; }
    public BoardEntity? Board { get; set; }
}
