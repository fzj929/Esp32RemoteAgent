using System.Net.Sockets;

namespace RelayServer.Relay;

public sealed record PublicConnection(uint Id, TcpClient Client);
