using Microsoft.Data.Sqlite;
using Microsoft.Extensions.Options;
using RelayServer.Options;
using RelayServer.Security;

namespace RelayServer.Data;

public sealed class AuthRepository(
    IOptions<RelayOptions> options,
    IOptions<AdminOptions> adminOptions,
    ILogger<AuthRepository> logger)
{
    private readonly string _connectionString = new SqliteConnectionStringBuilder
    {
        DataSource = Path.GetFullPath(options.Value.DatabasePath)
    }.ToString();

    public async Task InitializeAsync()
    {
        await using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        var command = connection.CreateCommand();
        command.CommandText = """
            CREATE TABLE IF NOT EXISTS admins (
                username TEXT PRIMARY KEY,
                password_hash TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            """;
        await command.ExecuteNonQueryAsync();

        var exists = connection.CreateCommand();
        exists.CommandText = "SELECT COUNT(*) FROM admins;";
        var count = (long)(await exists.ExecuteScalarAsync() ?? 0L);
        if (count == 0)
        {
            var username = string.IsNullOrWhiteSpace(adminOptions.Value.BootstrapUsername)
                ? "admin"
                : adminOptions.Value.BootstrapUsername.Trim();
            var password = adminOptions.Value.BootstrapPassword;
            if (string.IsNullOrWhiteSpace(password) || password.Length < 8)
            {
                throw new InvalidOperationException("Admin bootstrap password must be at least 8 characters.");
            }

            await SetPasswordAsync(username, password);
            logger.LogWarning("Initial admin created. Username: {Username}. Change the bootstrap password immediately.", username);
        }
    }

    public async Task<bool> ValidateAsync(string username, string password)
    {
        if (string.IsNullOrWhiteSpace(username) || string.IsNullOrEmpty(password))
        {
            return false;
        }

        var hash = await GetPasswordHashAsync(username.Trim());
        return hash is not null && PasswordHasher.Verify(password, hash);
    }

    public async Task SetPasswordAsync(string username, string password)
    {
        await using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        var command = connection.CreateCommand();
        command.CommandText = """
            INSERT INTO admins (username, password_hash, updated_at)
            VALUES ($username, $passwordHash, $updatedAt)
            ON CONFLICT(username) DO UPDATE SET
                password_hash = excluded.password_hash,
                updated_at = excluded.updated_at;
            """;
        command.Parameters.AddWithValue("$username", username.Trim());
        command.Parameters.AddWithValue("$passwordHash", PasswordHasher.Hash(password));
        command.Parameters.AddWithValue("$updatedAt", DateTimeOffset.UtcNow.ToString("O"));
        await command.ExecuteNonQueryAsync();
    }

    private async Task<string?> GetPasswordHashAsync(string username)
    {
        await using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        var command = connection.CreateCommand();
        command.CommandText = "SELECT password_hash FROM admins WHERE username = $username;";
        command.Parameters.AddWithValue("$username", username);
        return await command.ExecuteScalarAsync() as string;
    }
}
