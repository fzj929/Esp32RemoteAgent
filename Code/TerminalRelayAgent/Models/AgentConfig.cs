namespace TerminalRelayAgent.Models;

public sealed class AgentConfig
{
    public string BoardId { get; set; } = "S3-TERMINAL-0001";
    public string BoardKey { get; set; } = "CHANGE_THIS_DEVICE_SECRET";
    public string RelayHost { get; set; } = "127.0.0.1";
    public int RelayPort { get; set; } = 6555;
    public string DefaultTargetHost { get; set; } = "127.0.0.1";
    public int DefaultTargetPort { get; set; } = 3389;
    public bool Enabled { get; set; }
}
