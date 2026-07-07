using System.Net.Sockets;

namespace RelayServer.Relay;

public sealed class PublicConnection(uint id, TcpClient client)
{
    public uint Id { get; } = id;
    public TcpClient Client { get; } = client;
    public string? CloseReason { get; set; }
}
