using RelayServer.Relay;

namespace RelayServer.Endpoints;

public static class EventEndpoints
{
    public static IEndpointRouteBuilder MapEventEndpoints(this IEndpointRouteBuilder app)
    {
        app.MapGet("/api/events", (RelayHub hub) => hub.GetEvents().OrderByDescending(x => x.Timestamp).Take(200))
            .RequireAuthorization();

        return app;
    }
}
