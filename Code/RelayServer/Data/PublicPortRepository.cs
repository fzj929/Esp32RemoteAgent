using Microsoft.EntityFrameworkCore;
using RelayServer.Data.Entities;
using RelayServer.Models;

namespace RelayServer.Data;

public sealed class PublicPortRepository(IDbContextFactory<RelayDbContext> dbFactory)
{
    public async Task InitializeAsync()
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        var provider = db.Database.ProviderName ?? string.Empty;
        if (provider.Contains("MySql", StringComparison.OrdinalIgnoreCase))
        {
            await db.Database.ExecuteSqlRawAsync("""
                CREATE TABLE IF NOT EXISTS public_port_allocations (
                    public_port INT NOT NULL PRIMARY KEY,
                    customer_name VARCHAR(128) NOT NULL,
                    note VARCHAR(255) NULL,
                    enabled TINYINT(1) NOT NULL,
                    updated_at DATETIME(6) NOT NULL
                )
                """);
            return;
        }

        await db.Database.ExecuteSqlRawAsync("""
            CREATE TABLE IF NOT EXISTS public_port_allocations (
                public_port INTEGER NOT NULL PRIMARY KEY,
                customer_name TEXT NOT NULL,
                note TEXT NULL,
                enabled INTEGER NOT NULL,
                updated_at TEXT NOT NULL
            )
            """);
    }

    public async Task<IReadOnlyList<PublicPortAllocationDto>> GetPortsAsync()
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        var ports = await db.PublicPortAllocations
            .AsNoTracking()
            .OrderBy(x => x.PublicPort)
            .ToListAsync();
        var usages = await db.BoardServices
            .AsNoTracking()
            .Select(x => new { x.PublicPort, x.BoardId, x.Name })
            .ToListAsync();
        var usageByPort = usages
            .GroupBy(x => x.PublicPort)
            .ToDictionary(x => x.Key, x => x.First());

        return ports.Select(port =>
        {
            usageByPort.TryGetValue(port.PublicPort, out var usage);
            return new PublicPortAllocationDto(
                port.PublicPort,
                port.CustomerName,
                port.Note,
                port.Enabled,
                port.UpdatedAt,
                usage?.BoardId,
                usage?.Name);
        }).ToList();
    }

    public async Task UpsertAsync(PublicPortAllocationRequest request)
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        var entity = await db.PublicPortAllocations.FirstOrDefaultAsync(x => x.PublicPort == request.PublicPort);
        if (entity is null)
        {
            entity = new PublicPortAllocationEntity { PublicPort = request.PublicPort };
            db.PublicPortAllocations.Add(entity);
        }

        entity.CustomerName = request.CustomerName.Trim();
        entity.Note = string.IsNullOrWhiteSpace(request.Note) ? null : request.Note.Trim();
        entity.Enabled = request.Enabled;
        entity.UpdatedAt = DateTimeOffset.UtcNow;
        await db.SaveChangesAsync();
    }

    public async Task<string?> DeleteAsync(int publicPort)
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        var usage = await db.BoardServices
            .AsNoTracking()
            .FirstOrDefaultAsync(x => x.PublicPort == publicPort);
        if (usage is not null)
        {
            return $"Port {publicPort} is used by board {usage.BoardId}.";
        }

        var entity = await db.PublicPortAllocations.FirstOrDefaultAsync(x => x.PublicPort == publicPort);
        if (entity is null)
        {
            return null;
        }

        db.PublicPortAllocations.Remove(entity);
        await db.SaveChangesAsync();
        return null;
    }

    public async Task<string?> ValidateServicePortsAsync(string boardId, IReadOnlyList<BoardServiceEditRequest> services)
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        var ports = services.Select(x => x.PublicPort).Distinct().ToList();
        var configuredPorts = await db.PublicPortAllocations
            .AsNoTracking()
            .Where(x => ports.Contains(x.PublicPort))
            .ToListAsync();
        var configuredByPort = configuredPorts.ToDictionary(x => x.PublicPort);

        foreach (var port in ports)
        {
            if (!configuredByPort.TryGetValue(port, out var configured))
            {
                return $"Public port {port} is not configured for a customer.";
            }

            if (!configured.Enabled)
            {
                return $"Public port {port} is disabled.";
            }
        }

        var conflict = await db.BoardServices
            .AsNoTracking()
            .Where(x => ports.Contains(x.PublicPort) && x.BoardId != boardId)
            .OrderBy(x => x.PublicPort)
            .FirstOrDefaultAsync();
        return conflict is null
            ? null
            : $"Public port {conflict.PublicPort} is already used by board {conflict.BoardId}.";
    }
}
