using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Options;
using RelayServer.Data.Entities;
using RelayServer.Models;
using RelayServer.Options;
using RelayServer.Security;

namespace RelayServer.Data;

public sealed class AuthRepository(
    IDbContextFactory<RelayDbContext> dbFactory,
    IOptions<AdminOptions> adminOptions,
    ILogger<AuthRepository> logger)
{
    public async Task InitializeAsync()
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        await db.Database.EnsureCreatedAsync();

        if (!await db.Users.AnyAsync())
        {
            var username = string.IsNullOrWhiteSpace(adminOptions.Value.BootstrapUsername)
                ? "admin"
                : adminOptions.Value.BootstrapUsername.Trim();
            var password = adminOptions.Value.BootstrapPassword;
            if (string.IsNullOrWhiteSpace(password) || password.Length < 8)
            {
                throw new InvalidOperationException("Admin bootstrap password must be at least 8 characters.");
            }

            await CreateOrUpdateUserAsync(username, password, UserRoles.Administrator);
            logger.LogWarning("Initial admin created. Username: {Username}. Change the bootstrap password immediately.", username);
        }
    }

    public async Task<UserRecord?> ValidateUserAsync(string username, string password)
    {
        if (string.IsNullOrWhiteSpace(username) || string.IsNullOrEmpty(password))
        {
            return null;
        }

        var user = await GetUserAsync(username.Trim());
        return user is not null && PasswordHasher.Verify(password, user.PasswordHash)
            ? user
            : null;
    }

    public async Task<bool> ValidateAsync(string username, string password) =>
        await ValidateUserAsync(username, password) is not null;

    public async Task<IReadOnlyList<UserRecord>> GetUsersAsync()
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        return await db.Users
            .AsNoTracking()
            .OrderBy(x => x.Role)
            .ThenBy(x => x.Username)
            .Select(x => ToRecord(x))
            .ToListAsync();
    }

    public async Task<UserRecord?> GetUserAsync(string username)
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        var normalized = username.Trim();
        var user = await db.Users.AsNoTracking().FirstOrDefaultAsync(x => x.Username == normalized);
        return user is null ? null : ToRecord(user);
    }

    public async Task CreateOrUpdateUserAsync(string username, string password, string role)
    {
        var normalizedRole = NormalizeRole(role);
        await using var db = await dbFactory.CreateDbContextAsync();
        var normalizedUsername = username.Trim();
        var user = await db.Users.FirstOrDefaultAsync(x => x.Username == normalizedUsername);
        if (user is null)
        {
            user = new UserEntity { Username = normalizedUsername };
            db.Users.Add(user);
        }

        user.PasswordHash = PasswordHasher.Hash(password);
        user.Role = normalizedRole;
        user.UpdatedAt = DateTimeOffset.UtcNow;
        await db.SaveChangesAsync();
    }

    public async Task UpdateUserAsync(string username, string role)
    {
        var normalizedRole = NormalizeRole(role);
        await using var db = await dbFactory.CreateDbContextAsync();
        var normalizedUsername = username.Trim();
        var user = await db.Users.FirstOrDefaultAsync(x => x.Username == normalizedUsername);
        if (user is null)
        {
            return;
        }

        user.Role = normalizedRole;
        user.UpdatedAt = DateTimeOffset.UtcNow;
        await db.SaveChangesAsync();
    }

    public async Task SetPasswordAsync(string username, string password)
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        var normalizedUsername = username.Trim();
        var user = await db.Users.FirstOrDefaultAsync(x => x.Username == normalizedUsername);
        if (user is null)
        {
            return;
        }

        user.PasswordHash = PasswordHasher.Hash(password);
        user.UpdatedAt = DateTimeOffset.UtcNow;
        await db.SaveChangesAsync();
    }

    public static string NormalizeRole(string role) =>
        string.Equals(role, UserRoles.Administrator, StringComparison.OrdinalIgnoreCase)
            ? UserRoles.Administrator
            : UserRoles.User;

    private static UserRecord ToRecord(UserEntity user) =>
        new(user.Username, user.PasswordHash, user.Role, user.UpdatedAt);
}
