using Microsoft.Data.Sqlite;
using Microsoft.Extensions.Options;
using RelayServer.Models;
using RelayServer.Options;

namespace RelayServer.Data;

public sealed class BoardRepository(IOptions<RelayOptions> options)
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
            CREATE TABLE IF NOT EXISTS boards (
                board_id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                auth_key TEXT NOT NULL,
                assigned_port INTEGER NOT NULL UNIQUE,
                enabled INTEGER NOT NULL,
                target_host TEXT NOT NULL,
                target_port INTEGER NOT NULL,
                updated_at TEXT NOT NULL
            );
            """;
        await command.ExecuteNonQueryAsync();
    }

    public async Task<IReadOnlyList<BoardRecord>> GetBoardsAsync()
    {
        var boards = new List<BoardRecord>();
        await using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        var command = connection.CreateCommand();
        command.CommandText = """
            SELECT board_id, name, auth_key, assigned_port, enabled, target_host, target_port, updated_at
            FROM boards
            ORDER BY assigned_port;
            """;

        await using var reader = await command.ExecuteReaderAsync();
        while (await reader.ReadAsync())
        {
            boards.Add(ReadBoard(reader));
        }

        return boards;
    }

    public async Task<BoardRecord?> GetBoardAsync(string boardId)
    {
        await using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        var command = connection.CreateCommand();
        command.CommandText = """
            SELECT board_id, name, auth_key, assigned_port, enabled, target_host, target_port, updated_at
            FROM boards
            WHERE board_id = $boardId;
            """;
        command.Parameters.AddWithValue("$boardId", boardId);

        await using var reader = await command.ExecuteReaderAsync();
        return await reader.ReadAsync() ? ReadBoard(reader) : null;
    }

    public async Task UpsertBoardAsync(BoardRecord board)
    {
        await using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        var command = connection.CreateCommand();
        command.CommandText = """
            INSERT INTO boards (board_id, name, auth_key, assigned_port, enabled, target_host, target_port, updated_at)
            VALUES ($boardId, $name, $authKey, $assignedPort, $enabled, $targetHost, $targetPort, $updatedAt)
            ON CONFLICT(board_id) DO UPDATE SET
                name = excluded.name,
                auth_key = excluded.auth_key,
                assigned_port = excluded.assigned_port,
                enabled = excluded.enabled,
                target_host = excluded.target_host,
                target_port = excluded.target_port,
                updated_at = excluded.updated_at;
            """;
        command.Parameters.AddWithValue("$boardId", board.BoardId);
        command.Parameters.AddWithValue("$name", board.Name);
        command.Parameters.AddWithValue("$authKey", board.AuthKey);
        command.Parameters.AddWithValue("$assignedPort", board.AssignedPort);
        command.Parameters.AddWithValue("$enabled", board.Enabled ? 1 : 0);
        command.Parameters.AddWithValue("$targetHost", board.TargetHost);
        command.Parameters.AddWithValue("$targetPort", board.TargetPort);
        command.Parameters.AddWithValue("$updatedAt", board.UpdatedAt.ToString("O"));
        await command.ExecuteNonQueryAsync();
    }

    public async Task DeleteBoardAsync(string boardId)
    {
        await using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        var command = connection.CreateCommand();
        command.CommandText = "DELETE FROM boards WHERE board_id = $boardId;";
        command.Parameters.AddWithValue("$boardId", boardId);
        await command.ExecuteNonQueryAsync();
    }

    private static BoardRecord ReadBoard(SqliteDataReader reader) =>
        new(
            reader.GetString(0),
            reader.GetString(1),
            reader.GetString(2),
            reader.GetInt32(3),
            reader.GetInt32(4) == 1,
            reader.GetString(5),
            reader.GetInt32(6),
            DateTimeOffset.Parse(reader.GetString(7)));
}
