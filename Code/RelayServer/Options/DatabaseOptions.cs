namespace RelayServer.Options;

public sealed class DatabaseOptions
{
    public string Provider { get; set; } = "Sqlite";
    public string ConnectionString { get; set; } = "Data Source=relay.db";
    public string ServerVersion { get; set; } = "8.0.36";
}
