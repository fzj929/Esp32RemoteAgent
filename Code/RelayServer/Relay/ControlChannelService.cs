using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Microsoft.Extensions.Options;
using RelayServer.Data;
using RelayServer.Models;
using RelayServer.Options;

namespace RelayServer.Relay;

public sealed class ControlChannelService(
    IServiceProvider services,
    IOptions<RelayOptions> options,
    ILogger<ControlChannelService> logger) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        var listener = new TcpListener(IPAddress.Any, options.Value.ControlPort);
        listener.Start();
        logger.LogInformation("Board control channel listening on {Port}.", options.Value.ControlPort);

        while (!stoppingToken.IsCancellationRequested)
        {
            var client = await listener.AcceptTcpClientAsync(stoppingToken);
            _ = Task.Run(() => HandleClientAsync(client, stoppingToken), stoppingToken);
        }
    }

    private async Task HandleClientAsync(TcpClient client, CancellationToken stoppingToken)
    {
        await using var scope = services.CreateAsyncScope();
        var repo = scope.ServiceProvider.GetRequiredService<BoardRepository>();
        var hub = scope.ServiceProvider.GetRequiredService<RelayHub>();
        var relayOptions = scope.ServiceProvider.GetRequiredService<IOptions<RelayOptions>>().Value;

        client.NoDelay = true;
        var remote = client.Client.RemoteEndPoint?.ToString() ?? "unknown";
        var stream = client.GetStream();

        try
        {
            var first = await RelayFrame.ReadAsync(stream, stoppingToken);
            if (first is null || first.Type != RelayFrameType.Register)
            {
                client.Close();
                return;
            }

            var register = JsonSerializer.Deserialize<BoardRegisterRequest>(first.Payload.Span, RelayFrame.JsonOptions);
            if (register is null)
            {
                client.Close();
                return;
            }

            var board = await repo.GetBoardAsync(register.BoardId);
            if (board is null)
            {
                await RelayFrame.WriteJsonAsync(stream, RelayFrameType.Error, 0, new { error = "board not registered" }, stoppingToken);
                client.Close();
                return;
            }

            if (!board.Enabled)
            {
                await RelayFrame.WriteJsonAsync(stream, RelayFrameType.Error, 0, new { error = "board disabled" }, stoppingToken);
                client.Close();
                return;
            }

            if (!CryptographicEquals(board.AuthKey, register.AuthKey))
            {
                await RelayFrame.WriteJsonAsync(stream, RelayFrameType.Error, 0, new { error = "auth failed" }, stoppingToken);
                client.Close();
                return;
            }

            if (board.AssignedPort != register.AssignedPort)
            {
                await RelayFrame.WriteJsonAsync(stream, RelayFrameType.Error, 0, new { error = "assigned port mismatch" }, stoppingToken);
                client.Close();
                return;
            }

            var session = new BoardSession(client, board, hub, relayOptions, remote);
            await hub.RegisterAsync(session);
            await RelayFrame.WriteJsonAsync(stream, RelayFrameType.RegisterAck, 0, new { ok = true, board.AssignedPort }, stoppingToken);
            await session.RunAsync(stoppingToken);
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception ex)
        {
            hub.AddEvent("error", $"Control connection {remote} failed: {ex.Message}");
        }
        finally
        {
            client.Close();
        }
    }

    private static bool CryptographicEquals(string left, string right)
    {
        var a = Encoding.UTF8.GetBytes(left);
        var b = Encoding.UTF8.GetBytes(right);
        return a.Length == b.Length && CryptographicOperations.FixedTimeEquals(a, b);
    }
}
