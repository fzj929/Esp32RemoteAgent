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
            username = user.Identity?.Name,
            role = user.FindFirstValue(ClaimTypes.Role),
        }).AllowAnonymous();

        group.MapPost("/login", async (LoginRequest request, AuthRepository auth, LoginRateLimiter limiter, HttpContext http) =>
        {
            var remoteIp = http.Connection.RemoteIpAddress?.ToString() ?? "unknown";
            var limiterKey = $"{remoteIp}:{request.Username.Trim()}";
            if (limiter.IsBlocked(limiterKey, out var retryAfter))
            {
                return Results.Json(new { error = "Too many login failures.", retryAfterSeconds = (int)Math.Ceiling(retryAfter.TotalSeconds) }, statusCode: StatusCodes.Status429TooManyRequests);
            }

            var authenticatedUser = await auth.ValidateUserAsync(request.Username, request.Password);
            if (authenticatedUser is null)
            {
                limiter.RecordFailure(limiterKey);
                return Results.Unauthorized();
            }

            limiter.RecordSuccess(limiterKey);
            var claims = new List<Claim>
            {
                new(ClaimTypes.Name, authenticatedUser.Username),
                new(ClaimTypes.Role, authenticatedUser.Role)
            };
            var identity = new ClaimsIdentity(claims, CookieAuthenticationDefaults.AuthenticationScheme);
            await http.SignInAsync(CookieAuthenticationDefaults.AuthenticationScheme, new ClaimsPrincipal(identity));
            return Results.Ok(new { username = authenticatedUser.Username, role = authenticatedUser.Role });
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

        group.MapGet("/users", async (AuthRepository auth) =>
        {
            var users = await auth.GetUsersAsync();
            return users.Select(UserDto.From);
        }).RequireAuthorization(policy => policy.RequireRole(UserRoles.Administrator));

        group.MapPost("/users", async (CreateUserRequest request, AuthRepository auth) =>
        {
            if (string.IsNullOrWhiteSpace(request.Username))
            {
                return Results.BadRequest(new { error = "Username is required." });
            }

            if (string.IsNullOrWhiteSpace(request.Password) || request.Password.Length < 8)
            {
                return Results.BadRequest(new { error = "Password must be at least 8 characters." });
            }

            var role = AuthRepository.NormalizeRole(request.Role);
            await auth.CreateOrUpdateUserAsync(request.Username, request.Password, role);
            return Results.Ok();
        }).RequireAuthorization(policy => policy.RequireRole(UserRoles.Administrator));

        group.MapPut("/users/{username}", async (string username, UpdateUserRequest request, AuthRepository auth) =>
        {
            var role = AuthRepository.NormalizeRole(request.Role);
            await auth.UpdateUserAsync(username, role);
            return Results.Ok();
        }).RequireAuthorization(policy => policy.RequireRole(UserRoles.Administrator));

        group.MapPost("/users/{username}/reset-password", async (string username, ResetPasswordRequest request, AuthRepository auth) =>
        {
            if (string.IsNullOrWhiteSpace(request.NewPassword) || request.NewPassword.Length < 8)
            {
                return Results.BadRequest(new { error = "New password must be at least 8 characters." });
            }

            await auth.SetPasswordAsync(username, request.NewPassword);
            return Results.Ok();
        }).RequireAuthorization(policy => policy.RequireRole(UserRoles.Administrator));

        return app;
    }
}
