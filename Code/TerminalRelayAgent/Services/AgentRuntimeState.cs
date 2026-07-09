using TerminalRelayAgent.Models;

namespace TerminalRelayAgent.Services;

public sealed class AgentRuntimeState(AgentConfigStore configStore)
{
    private readonly object _sync = new();
    private bool _online;
    private DateTimeOffset? _connectedAt;
    private DateTimeOffset? _lastHeartbeat;
    private int _activeTunnels;
    private long _bytesFromServer;
    private long _bytesFromTarget;
    private string? _lastError;

    public void SetOnline(bool online)
    {
        lock (_sync)
        {
            _online = online;
            _connectedAt = online ? DateTimeOffset.UtcNow : null;
            if (!online)
            {
                _activeTunnels = 0;
            }
        }
    }

    public void SetHeartbeat() => _lastHeartbeat = DateTimeOffset.UtcNow;
    public void SetActiveTunnels(int count) => _activeTunnels = count;
    public void AddBytesFromServer(long count) => Interlocked.Add(ref _bytesFromServer, count);
    public void AddBytesFromTarget(long count) => Interlocked.Add(ref _bytesFromTarget, count);
    public void SetError(string? error) => _lastError = error;

    public async Task<AgentStatus> GetStatusAsync()
    {
        var config = await configStore.GetAsync();
        lock (_sync)
        {
            return new AgentStatus(
                config.Enabled,
                _online,
                config.BoardId,
                $"{config.RelayHost}:{config.RelayPort}",
                _connectedAt,
                _lastHeartbeat,
                _activeTunnels,
                Interlocked.Read(ref _bytesFromServer),
                Interlocked.Read(ref _bytesFromTarget),
                _lastError);
        }
    }
}
