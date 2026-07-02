using Microsoft.Data.Sqlite;
using Microsoft.Extensions.Options;
using RelayServer.Models;
using RelayServer.Options;

namespace RelayServer.Data;

public sealed class EventRepository(IOptions<RelayOptions> options)
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
            CREATE TABLE IF NOT EXISTS relay_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp TEXT NOT NULL,
                level TEXT NOT NULL,
                message TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS ix_relay_events_timestamp ON relay_events(timestamp DESC);
            """;
        await command.ExecuteNonQueryAsync();
    }

    public async Task AddAsync(RelayEvent relayEvent)
    {
        await using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        var command = connection.CreateCommand();
        command.CommandText = """
            INSERT INTO relay_events (timestamp, level, message)
            VALUES ($timestamp, $level, $message);
            DELETE FROM relay_events
            WHERE id NOT IN (
                SELECT id FROM relay_events ORDER BY timestamp DESC LIMIT 1000
            );
            """;
        command.Parameters.AddWithValue("$timestamp", relayEvent.Timestamp.ToString("O"));
        command.Parameters.AddWithValue("$level", relayEvent.Level);
        command.Parameters.AddWithValue("$message", relayEvent.Message);
        await command.ExecuteNonQueryAsync();
    }

    public async Task<IReadOnlyList<RelayEvent>> GetRecentAsync(int take)
    {
        await using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        var command = connection.CreateCommand();
        command.CommandText = """
            SELECT timestamp, level, message
            FROM relay_events
            ORDER BY timestamp DESC
            LIMIT $take;
            """;
        command.Parameters.AddWithValue("$take", Math.Clamp(take, 1, 1000));

        var events = new List<RelayEvent>();
        await using var reader = await command.ExecuteReaderAsync();
        while (await reader.ReadAsync())
        {
            events.Add(new RelayEvent(
                DateTimeOffset.Parse(reader.GetString(0)),
                reader.GetString(1),
                reader.GetString(2)));
        }

        return events;
    }
}
