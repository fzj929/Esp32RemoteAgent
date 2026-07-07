using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using RelayServer.Models;
using RelayServer.Options;

namespace RelayServer.Relay;

public sealed class BoardSession
{
    private readonly TcpClient _boardClient;
    private readonly BoardRecord _board;
    private readonly RelayHub _hub;
    private readonly NetworkStream _boardStream;
    private readonly SemaphoreSlim _writeLock = new(1, 1);
    private readonly ConcurrentDictionary<uint, PublicConnection> _connections = new();
    private readonly ConcurrentDictionary<uint, TaskCompletionSource<string?>> _probeWaiters = new();
    private readonly List<TcpListener> _publicListeners = [];
    private readonly CancellationTokenSource _stopCts = new();
    private uint _nextConnectionId;
    private int _stopped;
    private long _bytesFromPublic;
    private long _bytesFromBoard;

    public BoardSession(TcpClient boardClient, BoardRecord board, RelayHub hub, RelayOptions options, string remoteEndPoint, string? firmware)
    {
        _boardClient = boardClient;
        _board = board;
        _hub = hub;
        _boardStream = boardClient.GetStream();
        RemoteEndPoint = remoteEndPoint;
        ConnectedAt = DateTimeOffset.UtcNow;
        LastHeartbeat = ConnectedAt;
        Firmware = firmware;
    }

    public string BoardId => _board.BoardId;
    public int AssignedPort => _board.AssignedPort;
    public IReadOnlyList<BoardServiceRecord> Services => _board.Services;
    public DateTimeOffset ConnectedAt { get; }
    public DateTimeOffset LastHeartbeat { get; private set; }
    public string RemoteEndPoint { get; }
    public int ActiveConnectionCount => _connections.Count;
    public string? Firmware { get; private set; }
    public string? LastError { get; private set; }
    public BoardTelemetry? Telemetry { get; private set; }
    public long BytesFromPublic => Interlocked.Read(ref _bytesFromPublic);
    public long BytesFromBoard => Interlocked.Read(ref _bytesFromBoard);

    public async Task RunAsync(CancellationToken externalToken)
    {
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(externalToken, _stopCts.Token);
        var token = linked.Token;

        try
        {
            var acceptTasks = StartPublicListeners(token);
            var readTask = ReadBoardFramesAsync(token);
            await Task.WhenAny(acceptTasks.Append(readTask));
        }
        finally
        {
            await StopAsync("session ended");
        }
    }

    public async Task StopAsync(string reason)
    {
        if (Interlocked.Exchange(ref _stopped, 1) == 1)
        {
            return;
        }

        if (!_stopCts.IsCancellationRequested)
        {
            await _stopCts.CancelAsync();
        }

        foreach (var listener in _publicListeners)
        {
            try { listener.Stop(); } catch { }
        }

        foreach (var pair in _connections)
        {
            await ClosePublicConnectionAsync(pair.Key, "session stopped");
        }

        foreach (var pair in _probeWaiters)
        {
            if (_probeWaiters.TryRemove(pair.Key, out var waiter))
            {
                waiter.TrySetResult("session stopped");
            }
        }

        try { _boardClient.Close(); } catch { }
        _hub.Unregister(this, reason);
    }

    public bool MatchesServices(IReadOnlyList<BoardServiceRecord> services)
    {
        var current = _board.Services.OrderBy(x => x.PublicPort).ToList();
        var next = services.OrderBy(x => x.PublicPort).ToList();
        if (current.Count != next.Count)
        {
            return false;
        }

        for (var i = 0; i < current.Count; i++)
        {
            if (current[i].PublicPort != next[i].PublicPort ||
                current[i].TargetPort != next[i].TargetPort ||
                current[i].Enabled != next[i].Enabled ||
                !string.Equals(current[i].TargetHost, next[i].TargetHost, StringComparison.OrdinalIgnoreCase) ||
                !string.Equals(current[i].Name, next[i].Name, StringComparison.Ordinal))
            {
                return false;
            }
        }

        return true;
    }

    public async Task<(bool Success, string? Error)> ProbeTargetAsync(BoardServiceRecord service, TimeSpan timeout, CancellationToken cancellationToken)
    {
        if (_stopCts.IsCancellationRequested)
        {
            return (false, "board session is stopped");
        }

        var id = Interlocked.Increment(ref _nextConnectionId);
        var waiter = new TaskCompletionSource<string?>(TaskCreationOptions.RunContinuationsAsynchronously);
        if (!_probeWaiters.TryAdd(id, waiter))
        {
            return (false, "probe connection id collision");
        }

        try
        {
            await RelayFrame.WriteJsonAsync(_boardStream, RelayFrameType.Open, id, new
            {
                host = service.TargetHost,
                port = service.TargetPort
            }, cancellationToken, _writeLock);

            using var timeoutCts = new CancellationTokenSource(timeout);
            using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, timeoutCts.Token);

            try
            {
                var error = await waiter.Task.WaitAsync(linked.Token);
                return string.IsNullOrWhiteSpace(error)
                    ? (true, null)
                    : (false, error);
            }
            catch (OperationCanceledException) when (timeoutCts.IsCancellationRequested && !cancellationToken.IsCancellationRequested)
            {
                return (true, null);
            }
        }
        finally
        {
            _probeWaiters.TryRemove(id, out _);
            await RelayFrame.WriteAsync(_boardStream, RelayFrameType.Close, id, ReadOnlyMemory<byte>.Empty, CancellationToken.None, _writeLock);
        }
    }

    private IReadOnlyList<Task> StartPublicListeners(CancellationToken token)
    {
        var tasks = new List<Task>();
        foreach (var service in _board.Services.Where(x => x.Enabled))
        {
            var listener = new TcpListener(IPAddress.Any, service.PublicPort);
            listener.Start(128);
            _publicListeners.Add(listener);
            _hub.AddEvent("info", $"Port {service.PublicPort} opened for board {_board.BoardId} service {service.Name} -> {service.TargetHost}:{service.TargetPort}.");
            tasks.Add(Task.Run(() => AcceptPublicClientsAsync(listener, service, token), token));
        }

        return tasks;
    }

    private async Task AcceptPublicClientsAsync(TcpListener listener, BoardServiceRecord service, CancellationToken token)
    {
        while (!token.IsCancellationRequested)
        {
            var client = await listener.AcceptTcpClientAsync(token);
            client.NoDelay = true;
            var id = Interlocked.Increment(ref _nextConnectionId);
            var publicConnection = new PublicConnection(id, client);
            if (!_connections.TryAdd(id, publicConnection))
            {
                client.Close();
                continue;
            }

            _hub.AddEvent("info", $"TCP client connected to board {_board.BoardId} service {service.Name}, connection {id}.");
            await RelayFrame.WriteJsonAsync(_boardStream, RelayFrameType.Open, id, new
            {
                host = service.TargetHost,
                port = service.TargetPort
            }, token, _writeLock);

            _ = Task.Run(() => PumpPublicToBoardAsync(publicConnection, token), token);
        }
    }

    private async Task PumpPublicToBoardAsync(PublicConnection connection, CancellationToken token)
    {
        var buffer = new byte[8192];
        try
        {
            var stream = connection.Client.GetStream();
            while (!token.IsCancellationRequested)
            {
                var read = await stream.ReadAsync(buffer, token);
                if (read <= 0)
                {
                    connection.CloseReason = "public client closed";
                    break;
                }

                Interlocked.Add(ref _bytesFromPublic, read);
                await RelayFrame.WriteAsync(_boardStream, RelayFrameType.Data, connection.Id, buffer.AsMemory(0, read), token, _writeLock);
            }
        }
        catch (OperationCanceledException)
        {
            connection.CloseReason = "session stopping";
        }
        catch (Exception ex)
        {
            connection.CloseReason = $"public read failed: {ex.GetType().Name}";
        }
        finally
        {
            await RelayFrame.WriteAsync(_boardStream, RelayFrameType.Close, connection.Id, ReadOnlyMemory<byte>.Empty, CancellationToken.None, _writeLock);
            await ClosePublicConnectionAsync(connection.Id, connection.CloseReason ?? "public pump ended");
        }
    }

    private async Task ReadBoardFramesAsync(CancellationToken token)
    {
        while (!token.IsCancellationRequested)
        {
            var frame = await RelayFrame.ReadAsync(_boardStream, token);
            if (frame is null)
            {
                break;
            }

            switch (frame.Type)
            {
                case RelayFrameType.Heartbeat:
                    LastHeartbeat = DateTimeOffset.UtcNow;
                    ParseHeartbeat(frame.Payload);
                    break;
                case RelayFrameType.Data:
                    Interlocked.Add(ref _bytesFromBoard, frame.Payload.Length);
                    await WriteToPublicClientAsync(frame.ConnectionId, frame.Payload, token);
                    break;
                case RelayFrameType.Close:
                    await ClosePublicConnectionAsync(frame.ConnectionId, "board closed tunnel");
                    break;
                case RelayFrameType.Error:
                    var message = frame.Payload.IsEmpty
                        ? "no detail"
                        : Encoding.UTF8.GetString(frame.Payload.Span);
                    if (_probeWaiters.TryRemove(frame.ConnectionId, out var probeWaiter))
                    {
                        probeWaiter.TrySetResult(message);
                        break;
                    }

                    LastError = message;
                    _hub.AddEvent("error", $"Board {_board.BoardId} reported connection {frame.ConnectionId} error: {message}.");
                    await ClosePublicConnectionAsync(frame.ConnectionId, $"board error: {message}");
                    break;
            }
        }
    }

    private async Task WriteToPublicClientAsync(uint connectionId, ReadOnlyMemory<byte> payload, CancellationToken token)
    {
        if (!_connections.TryGetValue(connectionId, out var connection))
        {
            return;
        }

        try
        {
            await connection.Client.GetStream().WriteAsync(payload, token);
        }
        catch
        {
            await ClosePublicConnectionAsync(connectionId, "public write failed");
        }
    }

    private Task ClosePublicConnectionAsync(uint connectionId, string reason)
    {
        if (_connections.TryRemove(connectionId, out var connection))
        {
            try { connection.Client.Close(); } catch { }
            _hub.AddEvent("info", $"TCP client disconnected from board {_board.BoardId}, connection {connectionId}: {reason}.");
        }

        return Task.CompletedTask;
    }

    private void ParseHeartbeat(ReadOnlyMemory<byte> payload)
    {
        if (payload.IsEmpty)
        {
            return;
        }

        try
        {
            using var doc = JsonDocument.Parse(payload);
            var root = doc.RootElement;
            var firmware = GetString(root, "firmware") ?? Firmware;
            Firmware = firmware;
            Telemetry = new BoardTelemetry(
                GetInt64(root, "uptimeMs"),
                GetInt64(root, "freeHeap"),
                GetInt32(root, "rssi"),
                GetInt32(root, "activeTunnels"),
                GetInt64(root, "bytesFromServer"),
                GetInt64(root, "bytesFromTerminal"),
                GetString(root, "usbNetif"),
                firmware);
        }
        catch (JsonException ex)
        {
            LastError = $"invalid heartbeat payload: {ex.Message}";
        }
    }

    private static string? GetString(JsonElement root, string name) =>
        root.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString()
            : null;

    private static long? GetInt64(JsonElement root, string name) =>
        root.TryGetProperty(name, out var value) && value.TryGetInt64(out var number)
            ? number
            : null;

    private static int? GetInt32(JsonElement root, string name) =>
        root.TryGetProperty(name, out var value) && value.TryGetInt32(out var number)
            ? number
            : null;
}
