using System.Text.Json;
using TerminalRelayAgent.Models;
using TerminalRelayAgent.Relay;

namespace TerminalRelayAgent.Services;

public sealed class AgentConfigStore(IConfiguration configuration, IWebHostEnvironment environment)
{
    private readonly string _path = Path.Combine(environment.ContentRootPath, "agentsettings.json");
    private readonly SemaphoreSlim _lock = new(1, 1);
    private AgentConfig? _cached;
    private long _version;

    public long Version => Interlocked.Read(ref _version);

    public async Task<AgentConfig> GetAsync()
    {
        await _lock.WaitAsync();
        try
        {
            if (_cached is not null)
            {
                return Clone(_cached);
            }

            AgentConfig config;
            if (File.Exists(_path))
            {
                var json = await File.ReadAllTextAsync(_path);
                config = JsonSerializer.Deserialize<AgentConfig>(json, RelayFrame.JsonOptions) ?? new AgentConfig();
            }
            else
            {
                config = configuration.GetSection("Agent").Get<AgentConfig>() ?? new AgentConfig();
                await WriteLockedAsync(config);
            }

            _cached = Normalize(config);
            return Clone(_cached);
        }
        finally
        {
            _lock.Release();
        }
    }

    public async Task<(bool Success, string? Error)> SaveAsync(AgentConfig config)
    {
        var validation = Validate(config);
        if (validation is not null)
        {
            return (false, validation);
        }

        await _lock.WaitAsync();
        try
        {
            _cached = Normalize(config);
            Interlocked.Increment(ref _version);
            await WriteLockedAsync(_cached);
            return (true, null);
        }
        finally
        {
            _lock.Release();
        }
    }

    private async Task WriteLockedAsync(AgentConfig config)
    {
        var json = JsonSerializer.Serialize(Normalize(config), RelayFrame.JsonOptions);
        await File.WriteAllTextAsync(_path, json);
    }

    private static string? Validate(AgentConfig config)
    {
        if (string.IsNullOrWhiteSpace(config.BoardId))
        {
            return "BoardId is required.";
        }

        if (string.IsNullOrWhiteSpace(config.BoardKey) || config.BoardKey == "CHANGE_THIS_DEVICE_SECRET")
        {
            return "BoardKey must be changed from the default placeholder.";
        }

        if (string.IsNullOrWhiteSpace(config.RelayHost))
        {
            return "RelayHost is required.";
        }

        if (config.RelayPort <= 0 || config.RelayPort > 65535)
        {
            return "RelayPort is invalid.";
        }

        if (string.IsNullOrWhiteSpace(config.DefaultTargetHost))
        {
            return "DefaultTargetHost is required.";
        }

        if (config.DefaultTargetPort <= 0 || config.DefaultTargetPort > 65535)
        {
            return "DefaultTargetPort is invalid.";
        }

        return null;
    }

    private static AgentConfig Normalize(AgentConfig config) => new()
    {
        BoardId = config.BoardId.Trim(),
        BoardKey = config.BoardKey.Trim(),
        RelayHost = config.RelayHost.Trim(),
        RelayPort = config.RelayPort,
        DefaultTargetHost = config.DefaultTargetHost.Trim(),
        DefaultTargetPort = config.DefaultTargetPort,
        Enabled = config.Enabled
    };

    private static AgentConfig Clone(AgentConfig config) => Normalize(config);
}
