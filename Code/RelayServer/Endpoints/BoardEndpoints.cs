using RelayServer.Data;
using RelayServer.Models;
using RelayServer.Options;
using RelayServer.Relay;
using RelayServer.Validation;
using System.Diagnostics;
using System.Net.Sockets;

namespace RelayServer.Endpoints;

public static class BoardEndpoints
{
    public static IEndpointRouteBuilder MapBoardEndpoints(this IEndpointRouteBuilder app)
    {
        var group = app.MapGroup("/api/boards").RequireAuthorization();

        group.MapGet("/", async (BoardRepository repo, RelayHub hub) =>
        {
            var boards = await repo.GetBoardsAsync();
            return boards.Select(board => BoardDto.From(board, hub.GetSession(board.BoardId)));
        });

        group.MapGet("/diagnostics", async (BoardRepository repo, RelayHub hub) =>
        {
            var boards = await repo.GetBoardsAsync();
            var dto = boards.Select(board => BoardDto.From(board, hub.GetSession(board.BoardId))).ToList();
            return new RelayDiagnosticDto(
                dto.Count,
                dto.Count(x => x.Online),
                dto.Sum(x => x.ActiveConnections),
                dto.Sum(x => x.BytesFromPublic),
                dto.Sum(x => x.BytesFromBoard),
                DateTimeOffset.UtcNow,
                dto);
        });

        group.MapPost("/", async (BoardEditRequest request, BoardRepository repo, RelayOptions options) =>
        {
            var validation = BoardValidator.Validate(request, options);
            if (validation is not null)
            {
                return Results.BadRequest(new { error = validation });
            }

            await repo.UpsertBoardAsync(new BoardRecord(
                request.BoardId.Trim(),
                request.Name.Trim(),
                request.AuthKey.Trim(),
                request.AssignedPort,
                request.Enabled,
                request.TargetHost.Trim(),
                request.TargetPort,
                DateTimeOffset.UtcNow));

            return Results.Ok();
        });

        group.MapPut("/{boardId}", async (string boardId, BoardEditRequest request, BoardRepository repo, RelayOptions options, RelayHub hub) =>
        {
            if (!string.Equals(boardId, request.BoardId, StringComparison.Ordinal))
            {
                return Results.BadRequest(new { error = "URL boardId and payload boardId must match." });
            }

            var existing = await repo.GetBoardAsync(boardId);
            if (existing is null)
            {
                return Results.NotFound(new { error = "Board not found." });
            }

            var effectiveAuthKey = string.IsNullOrWhiteSpace(request.AuthKey) ? existing.AuthKey : request.AuthKey.Trim();
            var validation = BoardValidator.Validate(request with { AuthKey = effectiveAuthKey }, options);
            if (validation is not null)
            {
                return Results.BadRequest(new { error = validation });
            }

            await repo.UpsertBoardAsync(new BoardRecord(
                request.BoardId.Trim(),
                request.Name.Trim(),
                effectiveAuthKey,
                request.AssignedPort,
                request.Enabled,
                request.TargetHost.Trim(),
                request.TargetPort,
                DateTimeOffset.UtcNow));

            var online = hub.GetSession(request.BoardId);
            if (online is not null && (!request.Enabled || online.AssignedPort != request.AssignedPort))
            {
                await online.StopAsync("board configuration changed");
            }

            return Results.Ok();
        });

        group.MapDelete("/{boardId}", async (string boardId, BoardRepository repo, RelayHub hub) =>
        {
            var online = hub.GetSession(boardId);
            if (online is not null)
            {
                await online.StopAsync("board deleted");
            }

            await repo.DeleteBoardAsync(boardId);
            return Results.Ok();
        });

        group.MapPost("/{boardId}/disconnect", async (string boardId, RelayHub hub) =>
        {
            var online = hub.GetSession(boardId);
            if (online is null)
            {
                return Results.NotFound(new { error = "Board is not online." });
            }

            await online.StopAsync("manual disconnect");
            return Results.Ok();
        });

        group.MapPost("/{boardId}/probe-target", async (string boardId, BoardRepository repo) =>
        {
            var board = await repo.GetBoardAsync(boardId);
            if (board is null)
            {
                return Results.NotFound(new { error = "Board not found." });
            }

            var sw = Stopwatch.StartNew();
            try
            {
                using var client = new TcpClient { NoDelay = true };
                using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(3));
                await client.ConnectAsync(board.TargetHost, board.TargetPort, cts.Token);
                sw.Stop();
                return Results.Ok(new TargetProbeDto(board.BoardId, $"{board.TargetHost}:{board.TargetPort}", true, sw.ElapsedMilliseconds, null));
            }
            catch (Exception ex)
            {
                sw.Stop();
                return Results.Ok(new TargetProbeDto(board.BoardId, $"{board.TargetHost}:{board.TargetPort}", false, sw.ElapsedMilliseconds, ex.Message));
            }
        });

        return app;
    }
}
