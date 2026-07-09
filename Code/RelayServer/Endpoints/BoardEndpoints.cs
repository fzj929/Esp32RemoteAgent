using RelayServer.Data;
using RelayServer.Models;
using RelayServer.Options;
using RelayServer.Relay;
using RelayServer.Validation;
using System.Security.Claims;
using System.Diagnostics;

namespace RelayServer.Endpoints;

public static class BoardEndpoints
{
    public static IEndpointRouteBuilder MapBoardEndpoints(this IEndpointRouteBuilder app)
    {
        var group = app.MapGroup("/api/boards").RequireAuthorization();

        group.MapGet("/", async (BoardRepository repo, RelayHub hub, ClaimsPrincipal user) =>
        {
            var access = GetAccess(user);
            var boards = FilterBoards(await repo.GetBoardsAsync(), access);
            return boards.Select(board => BoardDto.From(board, hub.GetSession(board.BoardId)));
        });

        group.MapGet("/diagnostics", async (BoardRepository repo, RelayHub hub, ClaimsPrincipal user) =>
        {
            var access = GetAccess(user);
            var boards = FilterBoards(await repo.GetBoardsAsync(), access);
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

        group.MapPost("/", async (BoardEditRequest request, BoardRepository repo, PublicPortRepository ports, RelayOptions options, ClaimsPrincipal user) =>
        {
            var access = GetAccess(user);
            var normalizedRequest = NormalizeOwnership(request, access);
            var validation = BoardValidator.Validate(normalizedRequest, options);
            if (validation is not null)
            {
                return Results.BadRequest(new { error = validation });
            }

            var services = BoardValidator.NormalizeServices(normalizedRequest);
            var portValidation = await ports.ValidateServicePortsAsync(normalizedRequest.BoardId.Trim(), services);
            if (portValidation is not null)
            {
                return Results.BadRequest(new { error = portValidation });
            }

            await repo.UpsertBoardAsync(ToBoardRecord(normalizedRequest, normalizedRequest.AuthKey.Trim()));

            return Results.Ok();
        });

        group.MapPut("/{boardId}", async (string boardId, BoardEditRequest request, BoardRepository repo, PublicPortRepository ports, RelayOptions options, RelayHub hub, ClaimsPrincipal user) =>
        {
            var access = GetAccess(user);
            if (!string.Equals(boardId, request.BoardId, StringComparison.Ordinal))
            {
                return Results.BadRequest(new { error = "URL boardId and payload boardId must match." });
            }

            var existing = await repo.GetBoardAsync(boardId);
            if (existing is null)
            {
                return Results.NotFound(new { error = "Board not found." });
            }
            if (!CanAccess(existing, access))
            {
                return Results.Forbid();
            }

            var normalizedRequest = NormalizeOwnership(request, access);
            var effectiveAuthKey = string.IsNullOrWhiteSpace(normalizedRequest.AuthKey) ? existing.AuthKey : normalizedRequest.AuthKey.Trim();
            var validation = BoardValidator.Validate(normalizedRequest with { AuthKey = effectiveAuthKey }, options);
            if (validation is not null)
            {
                return Results.BadRequest(new { error = validation });
            }

            var services = BoardValidator.NormalizeServices(normalizedRequest);
            var portValidation = await ports.ValidateServicePortsAsync(boardId, services);
            if (portValidation is not null)
            {
                return Results.BadRequest(new { error = portValidation });
            }

            var nextBoard = ToBoardRecord(normalizedRequest, effectiveAuthKey);
            await repo.UpsertBoardAsync(nextBoard);

            var online = hub.GetSession(normalizedRequest.BoardId);
            if (online is not null && (!normalizedRequest.Enabled || !online.MatchesServices(nextBoard.Services)))
            {
                await online.StopAsync("board configuration changed");
            }

            return Results.Ok();
        });

        group.MapDelete("/{boardId}", async (string boardId, BoardRepository repo, RelayHub hub, ClaimsPrincipal user) =>
        {
            var board = await repo.GetBoardAsync(boardId);
            if (board is null)
            {
                return Results.NotFound(new { error = "Board not found." });
            }
            if (!CanAccess(board, GetAccess(user)))
            {
                return Results.Forbid();
            }

            var online = hub.GetSession(boardId);
            if (online is not null)
            {
                await online.StopAsync("board deleted");
            }

            await repo.DeleteBoardAsync(boardId);
            return Results.Ok();
        });

        group.MapPost("/{boardId}/disconnect", async (string boardId, BoardRepository repo, RelayHub hub, ClaimsPrincipal user) =>
        {
            var board = await repo.GetBoardAsync(boardId);
            if (board is null)
            {
                return Results.NotFound(new { error = "Board not found." });
            }
            if (!CanAccess(board, GetAccess(user)))
            {
                return Results.Forbid();
            }

            var online = hub.GetSession(boardId);
            if (online is null)
            {
                return Results.NotFound(new { error = "Board is not online." });
            }

            await online.StopAsync("manual disconnect");
            return Results.Ok();
        });

        group.MapPost("/{boardId}/probe-target", async (string boardId, BoardRepository repo, RelayHub hub, ClaimsPrincipal user, CancellationToken cancellationToken) =>
        {
            var board = await repo.GetBoardAsync(boardId);
            if (board is null)
            {
                return Results.NotFound(new { error = "Board not found." });
            }
            if (!CanAccess(board, GetAccess(user)))
            {
                return Results.Forbid();
            }

            if (!board.Enabled)
            {
                return Results.Ok(new TargetProbeDto(board.BoardId, $"{board.TargetHost}:{board.TargetPort}", false, 0, "Board is disabled."));
            }

            var session = hub.GetSession(board.BoardId);
            if (session is null)
            {
                return Results.Ok(new TargetProbeDto(board.BoardId, $"{board.TargetHost}:{board.TargetPort}", false, 0, "Board is offline; target can only be tested through the board tunnel."));
            }

            var sw = Stopwatch.StartNew();
            var result = await session.ProbeTargetAsync(board.Services[0], TimeSpan.FromMilliseconds(3800), cancellationToken);
            sw.Stop();
            return Results.Ok(new TargetProbeDto(board.BoardId, $"{board.TargetHost}:{board.TargetPort}", result.Success, sw.ElapsedMilliseconds, result.Error));
        });

        group.MapPost("/{boardId}/services/{publicPort:int}/probe-target", async (string boardId, int publicPort, BoardRepository repo, RelayHub hub, ClaimsPrincipal user, CancellationToken cancellationToken) =>
        {
            var board = await repo.GetBoardAsync(boardId);
            if (board is null)
            {
                return Results.NotFound(new { error = "Board not found." });
            }
            if (!CanAccess(board, GetAccess(user)))
            {
                return Results.Forbid();
            }

            var service = board.Services.FirstOrDefault(x => x.PublicPort == publicPort);
            if (service is null)
            {
                return Results.NotFound(new { error = "Service not found." });
            }

            if (!board.Enabled || !service.Enabled)
            {
                return Results.Ok(new TargetProbeDto(board.BoardId, $"{service.TargetHost}:{service.TargetPort}", false, 0, "Service is disabled."));
            }

            var session = hub.GetSession(board.BoardId);
            if (session is null)
            {
                return Results.Ok(new TargetProbeDto(board.BoardId, $"{service.TargetHost}:{service.TargetPort}", false, 0, "Board is offline; target can only be tested through the board tunnel."));
            }

            var sw = Stopwatch.StartNew();
            var result = await session.ProbeTargetAsync(service, TimeSpan.FromMilliseconds(3800), cancellationToken);
            sw.Stop();
            return Results.Ok(new TargetProbeDto(board.BoardId, $"{service.TargetHost}:{service.TargetPort}", result.Success, sw.ElapsedMilliseconds, result.Error));
        });

        return app;
    }

    private static BoardRecord ToBoardRecord(BoardEditRequest request, string authKey)
    {
        var services = BoardValidator.NormalizeServices(request)
            .Select(service => new BoardServiceRecord(
                request.BoardId.Trim(),
                service.Name.Trim(),
                service.PublicPort,
                service.TargetHost.Trim(),
                service.TargetPort,
                service.Enabled))
            .ToList();
        var primary = services[0];

        return new BoardRecord(
            request.BoardId.Trim(),
            request.Name.Trim(),
            authKey,
            primary.PublicPort,
            request.Enabled,
            string.IsNullOrWhiteSpace(request.OwnerUsername) ? null : request.OwnerUsername.Trim(),
            primary.TargetHost,
            primary.TargetPort,
            DateTimeOffset.UtcNow,
            services);
    }

    private static AccessScope GetAccess(ClaimsPrincipal user)
    {
        var role = user.FindFirstValue(ClaimTypes.Role) ?? UserRoles.User;
        var username = user.Identity?.Name;
        return new AccessScope(string.Equals(role, UserRoles.Administrator, StringComparison.Ordinal), username);
    }

    private static IReadOnlyList<BoardRecord> FilterBoards(IReadOnlyList<BoardRecord> boards, AccessScope access) =>
        access.IsAdministrator
            ? boards
            : boards.Where(board => string.Equals(board.OwnerUsername, access.Username, StringComparison.Ordinal)).ToList();

    private static bool CanAccess(BoardRecord board, AccessScope access) =>
        access.IsAdministrator || string.Equals(board.OwnerUsername, access.Username, StringComparison.Ordinal);

    private static BoardEditRequest NormalizeOwnership(BoardEditRequest request, AccessScope access)
    {
        if (access.IsAdministrator)
        {
            return request with { OwnerUsername = string.IsNullOrWhiteSpace(request.OwnerUsername) ? null : request.OwnerUsername.Trim() };
        }

        return request with { OwnerUsername = access.Username };
    }

    private sealed record AccessScope(bool IsAdministrator, string? Username);
}
