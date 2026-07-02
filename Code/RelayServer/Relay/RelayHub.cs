using System.Collections.Concurrent;
using RelayServer.Data;
using RelayServer.Models;

namespace RelayServer.Relay;

public sealed class RelayHub(ILogger<RelayHub> logger, EventRepository eventRepository)
{
    private readonly ConcurrentDictionary<string, BoardSession> _sessions = new(StringComparer.Ordinal);
    private readonly ConcurrentQueue<RelayEvent> _events = new();

    public BoardSession? GetSession(string boardId) => _sessions.TryGetValue(boardId, out var session) ? session : null;

    public IReadOnlyList<RelayEvent> GetEvents() => _events.ToArray();

    public async Task RegisterAsync(BoardSession session)
    {
        if (_sessions.TryRemove(session.BoardId, out var oldSession))
        {
            await oldSession.StopAsync("replaced by new connection");
        }

        if (!_sessions.TryAdd(session.BoardId, session))
        {
            throw new InvalidOperationException($"Could not register board {session.BoardId}.");
        }

        AddEvent("info", $"Board {session.BoardId} online on port {session.AssignedPort}.");
    }

    public void Unregister(BoardSession session, string reason)
    {
        _sessions.TryRemove(new KeyValuePair<string, BoardSession>(session.BoardId, session));
        AddEvent("warn", $"Board {session.BoardId} offline: {reason}.");
    }

    public void AddEvent(string level, string message)
    {
        logger.LogInformation("[{Level}] {Message}", level, message);
        var relayEvent = new RelayEvent(DateTimeOffset.UtcNow, level, message);
        _events.Enqueue(relayEvent);
        _ = Task.Run(async () =>
        {
            try
            {
                await eventRepository.AddAsync(relayEvent);
            }
            catch (Exception ex)
            {
                logger.LogWarning(ex, "Failed to persist relay event.");
            }
        });
        while (_events.Count > 300 && _events.TryDequeue(out _))
        {
        }
    }
}
