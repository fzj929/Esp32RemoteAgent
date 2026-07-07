using Microsoft.EntityFrameworkCore;
using RelayServer.Data.Entities;
using RelayServer.Models;

namespace RelayServer.Data;

public sealed class EventRepository(IDbContextFactory<RelayDbContext> dbFactory)
{
    public async Task InitializeAsync()
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        await db.Database.EnsureCreatedAsync();
    }

    public async Task AddAsync(RelayEvent relayEvent)
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        db.RelayEvents.Add(new RelayEventEntity
        {
            Timestamp = relayEvent.Timestamp,
            Level = relayEvent.Level,
            Message = relayEvent.Message
        });
        await db.SaveChangesAsync();

        var stale = await db.RelayEvents
            .OrderByDescending(x => x.Id)
            .Skip(1000)
            .ToListAsync();
        if (stale.Count > 0)
        {
            db.RelayEvents.RemoveRange(stale);
            await db.SaveChangesAsync();
        }
    }

    public async Task<IReadOnlyList<RelayEvent>> GetRecentAsync(int take)
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        return await db.RelayEvents
            .AsNoTracking()
            .OrderByDescending(x => x.Id)
            .Take(Math.Clamp(take, 1, 1000))
            .Select(x => new RelayEvent(x.Timestamp, x.Level, x.Message))
            .ToListAsync();
    }
}
