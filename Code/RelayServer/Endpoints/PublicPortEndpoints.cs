using RelayServer.Data;
using RelayServer.Models;
using RelayServer.Options;
using System.Security.Claims;

namespace RelayServer.Endpoints;

public static class PublicPortEndpoints
{
    public static IEndpointRouteBuilder MapPublicPortEndpoints(this IEndpointRouteBuilder app)
    {
        var group = app.MapGroup("/api/public-ports").RequireAuthorization();

        group.MapGet("/", async (PublicPortRepository repo) => await repo.GetPortsAsync());

        group.MapPost("/", async (PublicPortAllocationRequest request, PublicPortRepository repo, RelayOptions options, ClaimsPrincipal user) =>
        {
            if (!IsAdmin(user))
            {
                return Results.Forbid();
            }

            var validation = Validate(request, options);
            if (validation is not null)
            {
                return Results.BadRequest(new { error = validation });
            }

            await repo.UpsertAsync(request);
            return Results.Ok();
        });

        group.MapPut("/{publicPort:int}", async (int publicPort, PublicPortAllocationRequest request, PublicPortRepository repo, RelayOptions options, ClaimsPrincipal user) =>
        {
            if (!IsAdmin(user))
            {
                return Results.Forbid();
            }

            if (publicPort != request.PublicPort)
            {
                return Results.BadRequest(new { error = "URL publicPort and payload publicPort must match." });
            }

            var validation = Validate(request, options);
            if (validation is not null)
            {
                return Results.BadRequest(new { error = validation });
            }

            await repo.UpsertAsync(request);
            return Results.Ok();
        });

        group.MapDelete("/{publicPort:int}", async (int publicPort, PublicPortRepository repo, ClaimsPrincipal user) =>
        {
            if (!IsAdmin(user))
            {
                return Results.Forbid();
            }

            var error = await repo.DeleteAsync(publicPort);
            return error is null ? Results.Ok() : Results.BadRequest(new { error });
        });

        return app;
    }

    private static string? Validate(PublicPortAllocationRequest request, RelayOptions options)
    {
        if (request.PublicPort < options.PublicPortMin || request.PublicPort > options.PublicPortMax)
        {
            return $"PublicPort must be between {options.PublicPortMin} and {options.PublicPortMax}.";
        }

        if (options.ReservedPorts.Contains(request.PublicPort))
        {
            return $"Port {request.PublicPort} is reserved.";
        }

        if (string.IsNullOrWhiteSpace(request.CustomerName))
        {
            return "CustomerName is required.";
        }

        return null;
    }

    private static bool IsAdmin(ClaimsPrincipal user)
    {
        var role = user.FindFirstValue(ClaimTypes.Role) ?? UserRoles.User;
        return string.Equals(role, UserRoles.Administrator, StringComparison.Ordinal);
    }
}
