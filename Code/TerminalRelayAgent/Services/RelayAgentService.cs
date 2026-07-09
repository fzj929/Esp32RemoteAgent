using System.Collections.Concurrent;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using TerminalRelayAgent.Models;
using TerminalRelayAgent.Relay;

namespace TerminalRelayAgent.Services;

public sealed class RelayAgentService(
    AgentConfigStore configStore,
    AgentRuntimeState state,
    ILogger<RelayAgentService> logger) : BackgroundService
{
    private const string Firmware = "terminal-agent-dotnet-1.0.0";
    private static readonly TimeSpan ReconnectDelay = TimeSpan.FromSeconds(3);
    private static readonly TimeSpan HeartbeatInterval = TimeSpan.FromSeconds(15);

    private readonly ConcurrentDictionary<uint, TunnelConnection> _tunnels = new();
    private readonly SemaphoreSlim _relayWriteLock = new(1, 1);
    private DateTimeOffset _lastHeartbeat = DateTimeOffset.MinValue;

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        while (!stoppingToken.IsCancellationRequested)
        {
            var config = await configStore.GetAsync();
            if (!config.Enabled)
            {
                state.SetOnline(false);
                await Task.Delay(ReconnectDelay, stoppingToken);
                continue;
            }

            try
            {
                await RunConnectionAsync(config, stoppingToken);
            }
            catch (OperationCanceledException) when (stoppingToken.IsCancellationRequested)
            {
            }
            catch (Exception ex)
            {
                state.SetError(ex.Message);
                logger.LogWarning(ex, "Relay connection failed.");
            }
            finally
            {
                CloseAllTunnels();
                state.SetOnline(false);
            }

            await Task.Delay(ReconnectDelay, stoppingToken);
        }
    }

    private async Task RunConnectionAsync(AgentConfig config, CancellationToken stoppingToken)
    {
        var configVersion = configStore.Version;
        using var relayClient = new TcpClient { NoDelay = true };
        logger.LogInformation("Connecting relay {Host}:{Port} as {BoardId}.", config.RelayHost, config.RelayPort, config.BoardId);
        await relayClient.ConnectAsync(config.RelayHost, config.RelayPort, stoppingToken);
        var stream = relayClient.GetStream();

        await RegisterAsync(stream, config, stoppingToken);
        await WaitRegisterAckAsync(stream, stoppingToken);
        state.SetOnline(true);
        state.SetError(null);
        _lastHeartbeat = DateTimeOffset.MinValue;

        var readTask = RelayFrame.ReadAsync(stream, stoppingToken);
        while (!stoppingToken.IsCancellationRequested)
        {
            var heartbeatTask = Task.Delay(TimeSpan.FromSeconds(1), stoppingToken);
            var completed = await Task.WhenAny(readTask, heartbeatTask);

            if (completed == readTask)
            {
                var frame = await readTask;
                if (frame is null)
                {
                    throw new IOException("Relay closed control connection.");
                }

                await HandleFrameAsync(stream, frame, config, stoppingToken);
                readTask = RelayFrame.ReadAsync(stream, stoppingToken);
            }

            await SendHeartbeatIfNeededAsync(stream, stoppingToken);
            if (configStore.Version != configVersion)
            {
                throw new InvalidOperationException("Agent configuration changed; reconnecting.");
            }
        }
    }

    private async Task RegisterAsync(NetworkStream stream, AgentConfig config, CancellationToken token)
    {
        var nonce = Convert.ToHexString(RandomNumberGenerator.GetBytes(8)).ToLowerInvariant();
        var timestampMs = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        var authPayload = string.Join('|',
            config.BoardId,
            "0",
            config.DefaultTargetHost,
            config.DefaultTargetPort.ToString(),
            Firmware,
            nonce,
            timestampMs.ToString());
        var signature = ComputeHmacHex(config.BoardKey, authPayload);

        await RelayFrame.WriteJsonAsync(stream, RelayFrameType.Register, 0, new
        {
            boardId = config.BoardId,
            assignedPort = 0,
            targetHost = config.DefaultTargetHost,
            targetPort = config.DefaultTargetPort,
            firmware = Firmware,
            authNonce = nonce,
            authTimestampMs = timestampMs,
            authSignature = signature
        }, token, _relayWriteLock);
    }

    private static async Task WaitRegisterAckAsync(NetworkStream stream, CancellationToken token)
    {
        var frame = await RelayFrame.ReadAsync(stream, token);
        if (frame is null)
        {
            throw new IOException("Relay closed before register ack.");
        }

        if (frame.Type != RelayFrameType.RegisterAck)
        {
            var text = Encoding.UTF8.GetString(frame.Payload.Span);
            throw new InvalidOperationException($"Registration failed: {text}");
        }
    }

    private async Task HandleFrameAsync(NetworkStream relayStream, RelayFrame frame, AgentConfig config, CancellationToken token)
    {
        switch (frame.Type)
        {
            case RelayFrameType.Open:
                await OpenTunnelAsync(relayStream, frame.ConnectionId, frame.Payload, config, token);
                break;
            case RelayFrameType.Data:
                await WriteToTargetAsync(relayStream, frame.ConnectionId, frame.Payload, token);
                break;
            case RelayFrameType.Close:
                CloseTunnel(frame.ConnectionId);
                break;
        }
    }

    private async Task OpenTunnelAsync(NetworkStream relayStream, uint connectionId, ReadOnlyMemory<byte> payload, AgentConfig config, CancellationToken token)
    {
        var host = config.DefaultTargetHost;
        var port = config.DefaultTargetPort;
        if (!payload.IsEmpty)
        {
            using var doc = JsonDocument.Parse(payload);
            if (doc.RootElement.TryGetProperty("host", out var hostNode) && hostNode.ValueKind == JsonValueKind.String)
            {
                host = hostNode.GetString() ?? host;
            }
            if (doc.RootElement.TryGetProperty("port", out var portNode) && portNode.TryGetInt32(out var requestedPort))
            {
                port = requestedPort;
            }
        }

        var cts = CancellationTokenSource.CreateLinkedTokenSource(token);
        var target = new TcpClient { NoDelay = true };
        try
        {
            await target.ConnectAsync(host, port, cts.Token);
            var tunnel = new TunnelConnection(connectionId, target, cts);
            if (!_tunnels.TryAdd(connectionId, tunnel))
            {
                target.Close();
                cts.Dispose();
                await RelayFrame.WriteTextAsync(relayStream, RelayFrameType.Error, connectionId, "connection id already exists", token, _relayWriteLock);
                await RelayFrame.WriteAsync(relayStream, RelayFrameType.Close, connectionId, ReadOnlyMemory<byte>.Empty, token, _relayWriteLock);
                return;
            }

            state.SetActiveTunnels(_tunnels.Count);
            logger.LogInformation("Tunnel {ConnectionId} connected to {Host}:{Port}.", connectionId, host, port);
            _ = Task.Run(() => PumpTargetToRelayAsync(relayStream, tunnel, cts.Token), CancellationToken.None);
        }
        catch (Exception ex)
        {
            target.Close();
            cts.Dispose();
            await RelayFrame.WriteTextAsync(relayStream, RelayFrameType.Error, connectionId, $"target connection failed: {ex.Message}", token, _relayWriteLock);
            await RelayFrame.WriteAsync(relayStream, RelayFrameType.Close, connectionId, ReadOnlyMemory<byte>.Empty, token, _relayWriteLock);
        }
    }

    private async Task WriteToTargetAsync(NetworkStream relayStream, uint connectionId, ReadOnlyMemory<byte> payload, CancellationToken token)
    {
        if (!_tunnels.TryGetValue(connectionId, out var tunnel))
        {
            await RelayFrame.WriteAsync(relayStream, RelayFrameType.Close, connectionId, ReadOnlyMemory<byte>.Empty, token, _relayWriteLock);
            return;
        }

        try
        {
            await tunnel.TargetClient.GetStream().WriteAsync(payload, token);
            state.AddBytesFromServer(payload.Length);
        }
        catch (Exception ex)
        {
            await RelayFrame.WriteTextAsync(relayStream, RelayFrameType.Error, connectionId, $"target write failed: {ex.Message}", token, _relayWriteLock);
            await RelayFrame.WriteAsync(relayStream, RelayFrameType.Close, connectionId, ReadOnlyMemory<byte>.Empty, token, _relayWriteLock);
            CloseTunnel(connectionId);
        }
    }

    private async Task PumpTargetToRelayAsync(NetworkStream relayStream, TunnelConnection tunnel, CancellationToken token)
    {
        var buffer = new byte[8192];
        try
        {
            var targetStream = tunnel.TargetClient.GetStream();
            while (!token.IsCancellationRequested)
            {
                var read = await targetStream.ReadAsync(buffer, token);
                if (read <= 0)
                {
                    break;
                }

                state.AddBytesFromTarget(read);
                await RelayFrame.WriteAsync(relayStream, RelayFrameType.Data, tunnel.Id, buffer.AsMemory(0, read), token, _relayWriteLock);
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception ex)
        {
            logger.LogDebug(ex, "Target pump failed for tunnel {ConnectionId}.", tunnel.Id);
        }
        finally
        {
            try
            {
                await RelayFrame.WriteAsync(relayStream, RelayFrameType.Close, tunnel.Id, ReadOnlyMemory<byte>.Empty, CancellationToken.None, _relayWriteLock);
            }
            catch (Exception ex)
            {
                logger.LogDebug(ex, "Sending close failed for tunnel {ConnectionId}.", tunnel.Id);
            }
            CloseTunnel(tunnel.Id);
        }
    }

    private async Task SendHeartbeatIfNeededAsync(NetworkStream stream, CancellationToken token)
    {
        if (DateTimeOffset.UtcNow - _lastHeartbeat < HeartbeatInterval)
        {
            return;
        }

        await RelayFrame.WriteJsonAsync(stream, RelayFrameType.Heartbeat, 0, new
        {
            uptimeMs = Environment.TickCount64,
            freeHeap = GC.GetTotalMemory(false),
            rssi = 0,
            activeTunnels = _tunnels.Count,
            bytesFromServer = (await state.GetStatusAsync()).BytesFromServer,
            bytesFromTerminal = (await state.GetStatusAsync()).BytesFromTarget,
            usbNetif = "terminal-direct",
            firmware = Firmware
        }, token, _relayWriteLock);
        _lastHeartbeat = DateTimeOffset.UtcNow;
        state.SetHeartbeat();
    }

    private void CloseTunnel(uint connectionId)
    {
        if (!_tunnels.TryRemove(connectionId, out var tunnel))
        {
            return;
        }

        try { tunnel.Cancellation.Cancel(); } catch { }
        try { tunnel.TargetClient.Close(); } catch { }
        tunnel.Cancellation.Dispose();
        state.SetActiveTunnels(_tunnels.Count);
    }

    private void CloseAllTunnels()
    {
        foreach (var id in _tunnels.Keys)
        {
            CloseTunnel(id);
        }
    }

    private static string ComputeHmacHex(string key, string payload)
    {
        using var hmac = new HMACSHA256(Encoding.UTF8.GetBytes(key));
        return Convert.ToHexString(hmac.ComputeHash(Encoding.UTF8.GetBytes(payload))).ToLowerInvariant();
    }
}
