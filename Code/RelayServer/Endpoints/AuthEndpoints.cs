using System.Security.Claims;
using Microsoft.AspNetCore.Authentication;
using Microsoft.AspNetCore.Authentication.Cookies;
using RelayServer.Data;
using RelayServer.Models;
using RelayServer.Security;

namespace RelayServer.Endpoints;

public static class AuthEndpoints
{
    public static IEndpointRouteBuilder MapAuthEndpoints(this IEndpointRouteBuilder app)
    {
        var group = app.MapGroup("/api/auth");

        group.MapGet("/status", (ClaimsPrincipal user) => new
        {
            authenticated = user.Identity?.IsAuthenticated == true,
            username = user.Identity?.Name
        }).AllowAnonymous();

        group.MapPost("/login", async (LoginRequest request, AuthRepository auth, LoginRateLimiter limiter, HttpContext http) =>
        {
            var remoteIp = http.Connection.RemoteIpAddress?.ToString() ?? "unknown";
            var limiterKey = $"{remoteIp}:{request.Username.Trim()}";
            if (limiter.IsBlocked(limiterKey, out var retryAfter))
            {
                return Results.Json(new { error = "Too many login failures.", retryAfterSeconds = (int)Math.Ceiling(retryAfter.TotalSeconds) }, statusCode: StatusCodes.Status429TooManyRequests);
            }

            if (!await auth.ValidateAsync(request.Username, request.Password))
            {
                limiter.RecordFailure(limiterKey);
                return Results.Unauthorized();
            }

            limiter.RecordSuccess(limiterKey);
            var claims = new List<Claim>
            {
                new(ClaimTypes.Name, request.Username.Trim()),
                new(ClaimTypes.Role, "Administrator")
            };
            var identity = new ClaimsIdentity(claims, CookieAuthenticationDefaults.AuthenticationScheme);
            await http.SignInAsync(CookieAuthenticationDefaults.AuthenticationScheme, new ClaimsPrincipal(identity));
            return Results.Ok(new { username = request.Username.Trim() });
        }).AllowAnonymous();

        group.MapPost("/logout", async (HttpContext http) =>
        {
            await http.SignOutAsync(CookieAuthenticationDefaults.AuthenticationScheme);
            return Results.Ok();
        }).RequireAuthorization();

        group.MapPost("/change-password", async (ChangePasswordRequest request, ClaimsPrincipal user, AuthRepository auth) =>
        {
            var username = user.Identity?.Name;
            if (string.IsNullOrWhiteSpace(username))
            {
                return Results.Unauthorized();
            }

            if (string.IsNullOrWhiteSpace(request.NewPassword) || request.NewPassword.Length < 8)
            {
                return Results.BadRequest(new { error = "New password must be at least 8 characters." });
            }

            if (!await auth.ValidateAsync(username, request.CurrentPassword))
            {
                return Results.BadRequest(new { error = "Current password is incorrect." });
            }

            await auth.SetPasswordAsync(username, request.NewPassword);
            return Results.Ok();
        }).RequireAuthorization();

        return app;
    }
}
