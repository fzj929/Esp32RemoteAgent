using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.AspNetCore.DataProtection;
using Microsoft.EntityFrameworkCore;
using RelayServer.Data;
using RelayServer.Endpoints;
using RelayServer.Options;
using RelayServer.Relay;
using RelayServer.Security;

var builder = WebApplication.CreateBuilder(args);

builder.Logging.ClearProviders();
builder.Logging.AddConsole();

builder.Services.Configure<RelayOptions>(builder.Configuration.GetSection("Relay"));
builder.Services.Configure<AdminOptions>(builder.Configuration.GetSection("Admin"));
builder.Services.Configure<DatabaseOptions>(builder.Configuration.GetSection("Database"));
builder.Services.AddSingleton(sp => sp.GetRequiredService<Microsoft.Extensions.Options.IOptions<RelayOptions>>().Value);

builder.Services.AddDbContextFactory<RelayDbContext>((sp, options) =>
{
    var database = sp.GetRequiredService<Microsoft.Extensions.Options.IOptions<DatabaseOptions>>().Value;
    if (string.Equals(database.Provider, "MySql", StringComparison.OrdinalIgnoreCase))
    {
        options.UseMySql(database.ConnectionString, ServerVersion.Parse(database.ServerVersion));
        return;
    }

    options.UseSqlite(database.ConnectionString);
});

builder.Services.AddSingleton<BoardRepository>();
builder.Services.AddSingleton<AuthRepository>();
builder.Services.AddSingleton<EventRepository>();
builder.Services.AddSingleton<RelayHub>();
builder.Services.AddSingleton<LoginRateLimiter>();
builder.Services.AddHostedService<ControlChannelService>();
builder.Services.AddHostedService<SessionMaintenanceService>();
builder.Services.AddEndpointsApiExplorer();

builder.Services.AddAuthentication(CookieAuthenticationDefaults.AuthenticationScheme)
    .AddCookie(options =>
    {
        options.Cookie.Name = "RelayAdmin";
        options.Cookie.HttpOnly = true;
        options.Cookie.SameSite = SameSiteMode.Strict;
        options.SlidingExpiration = true;
        options.ExpireTimeSpan = TimeSpan.FromHours(8);
        options.Events.OnRedirectToLogin = context =>
        {
            context.Response.StatusCode = StatusCodes.Status401Unauthorized;
            return Task.CompletedTask;
        };
    });
builder.Services.AddAuthorization();
builder.Services.AddDataProtection()
    .PersistKeysToFileSystem(new DirectoryInfo(Path.Combine(builder.Environment.ContentRootPath, "DataProtectionKeys")))
    .SetApplicationName("Esp32RemoteRelay");

var app = builder.Build();

await app.Services.GetRequiredService<BoardRepository>().InitializeAsync();
await app.Services.GetRequiredService<AuthRepository>().InitializeAsync();
await app.Services.GetRequiredService<EventRepository>().InitializeAsync();

app.UseDefaultFiles();
app.UseStaticFiles();
app.UseAuthentication();
app.UseAuthorization();

app.MapAuthEndpoints();
app.MapBoardEndpoints();
app.MapEventEndpoints();

app.Run();
