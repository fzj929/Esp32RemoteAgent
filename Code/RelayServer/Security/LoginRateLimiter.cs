using System.Collections.Concurrent;

namespace RelayServer.Security;

public sealed class LoginRateLimiter
{
    private readonly ConcurrentDictionary<string, LoginAttempt> _attempts = new(StringComparer.OrdinalIgnoreCase);
    private readonly TimeSpan _window = TimeSpan.FromMinutes(5);
    private readonly int _maxFailures = 5;

    public bool IsBlocked(string key, out TimeSpan retryAfter)
    {
        retryAfter = TimeSpan.Zero;
        if (!_attempts.TryGetValue(key, out var attempt))
        {
            return false;
        }

        var now = DateTimeOffset.UtcNow;
        if (now - attempt.FirstFailureAt > _window)
        {
            _attempts.TryRemove(key, out _);
            return false;
        }

        if (attempt.Failures < _maxFailures)
        {
            return false;
        }

        retryAfter = _window - (now - attempt.FirstFailureAt);
        return true;
    }

    public void RecordFailure(string key)
    {
        var now = DateTimeOffset.UtcNow;
        _attempts.AddOrUpdate(
            key,
            _ => new LoginAttempt(1, now),
            (_, existing) => now - existing.FirstFailureAt > _window
                ? new LoginAttempt(1, now)
                : existing with { Failures = existing.Failures + 1 });
    }

    public void RecordSuccess(string key)
    {
        _attempts.TryRemove(key, out _);
    }

    private sealed record LoginAttempt(int Failures, DateTimeOffset FirstFailureAt);
}
