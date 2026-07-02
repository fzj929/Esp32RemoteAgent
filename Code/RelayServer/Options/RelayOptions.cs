namespace RelayServer.Options;

public sealed class RelayOptions
{
    public int ControlPort { get; set; } = 6555;
    public int PublicPortMin { get; set; } = 6500;
    public int PublicPortMax { get; set; } = 6600;
    public int[] ReservedPorts { get; set; } = [6555];
    public string DefaultTargetHost { get; set; } = "192.168.77.2";
    public int DefaultTargetPort { get; set; } = 3389;
    public int HeartbeatTimeoutSeconds { get; set; } = 45;
    public string DatabasePath { get; set; } = "relay.db";
}
