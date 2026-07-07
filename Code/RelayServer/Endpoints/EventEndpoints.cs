using RelayServer.Data;
using RelayServer.Models;
using RelayServer.Relay;

namespace RelayServer.Endpoints;

public static class EventEndpoints
{
    public static IEndpointRouteBuilder MapEventEndpoints(this IEndpointRouteBuilder app)
    {
        app.MapGet("/api/events", async (EventRepository events, RelayHub hub) =>
        {
            var persisted = await events.GetRecentAsync(200);
            return persisted.Count > 0
                ? persisted
                : hub.GetEvents().OrderByDescending(x => x.Timestamp).Take(200);
        })
            .RequireAuthorization(policy => policy.RequireRole(UserRoles.Administrator));

        return app;
    }
}
