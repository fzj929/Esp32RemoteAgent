using Microsoft.AspNetCore.Authentication.Cookies;
using Microsoft.AspNetCore.DataProtection;
using RelayServer.Data;
using RelayServer.Endpoints;
using RelayServer.Options;
using RelayServer.Relay;

var builder = WebApplication.CreateBuilder(args);

builder.Logging.ClearProviders();
builder.Logging.AddConsole();

builder.Services.Configure<RelayOptions>(builder.Configuration.GetSection("Relay"));
builder.Services.Configure<AdminOptions>(builder.Configuration.GetSection("Admin"));
builder.Services.AddSingleton(sp => sp.GetRequiredService<Microsoft.Extensions.Options.IOptions<RelayOptions>>().Value);

builder.Services.AddSingleton<BoardRepository>();
builder.Services.AddSingleton<AuthRepository>();
builder.Services.AddSingleton<RelayHub>();
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

app.UseDefaultFiles();
app.UseStaticFiles();
app.UseAuthentication();
app.UseAuthorization();

app.MapAuthEndpoints();
app.MapBoardEndpoints();
app.MapEventEndpoints();

app.Run();
