using System.Net.Sockets;

namespace TerminalRelayAgent.Services;

public sealed class TunnelConnection(uint id, TcpClient targetClient, CancellationTokenSource cts)
{
    public uint Id { get; } = id;
    public TcpClient TargetClient { get; } = targetClient;
    public CancellationTokenSource Cancellation { get; } = cts;
}
