using Microsoft.EntityFrameworkCore;
using RelayServer.Data.Entities;
using RelayServer.Models;

namespace RelayServer.Data;

public sealed class BoardRepository(IDbContextFactory<RelayDbContext> dbFactory)
{
    public async Task InitializeAsync()
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        await db.Database.EnsureCreatedAsync();
    }

    public async Task<IReadOnlyList<BoardRecord>> GetBoardsAsync()
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        var boards = await db.Boards
            .AsNoTracking()
            .Include(x => x.Services)
            .OrderBy(x => x.AssignedPort)
            .ToListAsync();

        return boards.Select(ToRecord).ToList();
    }

    public async Task<BoardRecord?> GetBoardAsync(string boardId)
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        var board = await db.Boards
            .AsNoTracking()
            .Include(x => x.Services)
            .FirstOrDefaultAsync(x => x.BoardId == boardId);

        return board is null ? null : ToRecord(board);
    }

    public async Task UpsertBoardAsync(BoardRecord board)
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        var existing = await db.Boards.Include(x => x.Services).FirstOrDefaultAsync(x => x.BoardId == board.BoardId);
        if (existing is null)
        {
            existing = new BoardEntity { BoardId = board.BoardId };
            db.Boards.Add(existing);
        }

        existing.Name = board.Name;
        existing.AuthKey = board.AuthKey;
        existing.AssignedPort = board.AssignedPort;
        existing.Enabled = board.Enabled;
        existing.OwnerUsername = board.OwnerUsername;
        existing.TargetHost = board.TargetHost;
        existing.TargetPort = board.TargetPort;
        existing.UpdatedAt = board.UpdatedAt;
        db.BoardServices.RemoveRange(existing.Services);
        existing.Services = board.Services.Select(service => new BoardServiceEntity
        {
            BoardId = board.BoardId,
            Name = service.Name,
            PublicPort = service.PublicPort,
            TargetHost = service.TargetHost,
            TargetPort = service.TargetPort,
            Enabled = service.Enabled
        }).ToList();

        await db.SaveChangesAsync();
    }

    public async Task DeleteBoardAsync(string boardId)
    {
        await using var db = await dbFactory.CreateDbContextAsync();
        var board = await db.Boards.Include(x => x.Services).FirstOrDefaultAsync(x => x.BoardId == boardId);
        if (board is null)
        {
            return;
        }

        db.BoardServices.RemoveRange(board.Services);
        db.Boards.Remove(board);
        await db.SaveChangesAsync();
    }

    private static BoardRecord ToRecord(BoardEntity board)
    {
        var services = board.Services
            .OrderBy(x => x.PublicPort)
            .Select(x => new BoardServiceRecord(board.BoardId, x.Name, x.PublicPort, x.TargetHost, x.TargetPort, x.Enabled))
            .ToList();
        if (services.Count == 0)
        {
            services.Add(new BoardServiceRecord(board.BoardId, "RDP", board.AssignedPort, board.TargetHost, board.TargetPort, true));
        }

        var primary = services[0];
        return new BoardRecord(
            board.BoardId,
            board.Name,
            board.AuthKey,
            primary.PublicPort,
            board.Enabled,
            board.OwnerUsername,
            primary.TargetHost,
            primary.TargetPort,
            board.UpdatedAt,
            services);
    }
}
