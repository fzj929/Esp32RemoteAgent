using Microsoft.Extensions.Options;
using RelayServer.Data;
using RelayServer.Options;

namespace RelayServer.Relay;

public sealed class SessionMaintenanceService(
    RelayHub hub,
    BoardRepository repository,
    IOptions<RelayOptions> options) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        while (!stoppingToken.IsCancellationRequested)
        {
            var boards = await repository.GetBoardsAsync();
            var timeout = TimeSpan.FromSeconds(options.Value.HeartbeatTimeoutSeconds);
            foreach (var board in boards)
            {
                var session = hub.GetSession(board.BoardId);
                if (session is not null && DateTimeOffset.UtcNow - session.LastHeartbeat > timeout)
                {
                    await session.StopAsync("heartbeat timeout");
                }
            }

            await Task.Delay(TimeSpan.FromSeconds(5), stoppingToken);
        }
    }
}
