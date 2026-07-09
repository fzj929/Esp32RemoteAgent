using TerminalRelayAgent.Models;
using TerminalRelayAgent.Services;

var builder = WebApplication.CreateBuilder(args);

builder.Logging.ClearProviders();
builder.Logging.AddConsole();
builder.Services.AddSingleton<AgentConfigStore>();
builder.Services.AddSingleton<AgentRuntimeState>();
builder.Services.AddHostedService<RelayAgentService>();

var app = builder.Build();

app.UseDefaultFiles();
app.UseStaticFiles();

app.MapGet("/api/config", async (AgentConfigStore store) => await store.GetAsync());
app.MapPut("/api/config", async (AgentConfig config, AgentConfigStore store) =>
{
    var result = await store.SaveAsync(config);
    return result.Success ? Results.Ok() : Results.BadRequest(new { error = result.Error });
});
app.MapGet("/api/status", async (AgentRuntimeState state) => await state.GetStatusAsync());

app.Run();
