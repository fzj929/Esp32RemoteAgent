using Microsoft.EntityFrameworkCore;
using RelayServer.Data.Entities;

namespace RelayServer.Data;

public sealed class RelayDbContext(DbContextOptions<RelayDbContext> options) : DbContext(options)
{
    public DbSet<UserEntity> Users => Set<UserEntity>();
    public DbSet<BoardEntity> Boards => Set<BoardEntity>();
    public DbSet<BoardServiceEntity> BoardServices => Set<BoardServiceEntity>();
    public DbSet<RelayEventEntity> RelayEvents => Set<RelayEventEntity>();

    protected override void OnModelCreating(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<UserEntity>(entity =>
        {
            entity.ToTable("users");
            entity.HasKey(x => x.Username);
            entity.Property(x => x.Username).HasColumnName("username").HasMaxLength(64);
            entity.Property(x => x.PasswordHash).HasColumnName("password_hash").IsRequired();
            entity.Property(x => x.Role).HasColumnName("role").HasMaxLength(32).IsRequired();
            entity.Property(x => x.UpdatedAt).HasColumnName("updated_at").IsRequired();
        });

        modelBuilder.Entity<BoardEntity>(entity =>
        {
            entity.ToTable("boards");
            entity.HasKey(x => x.BoardId);
            entity.HasIndex(x => x.AssignedPort).IsUnique();
            entity.Property(x => x.BoardId).HasColumnName("board_id").HasMaxLength(64);
            entity.Property(x => x.Name).HasColumnName("name").HasMaxLength(128).IsRequired();
            entity.Property(x => x.AuthKey).HasColumnName("auth_key").IsRequired();
            entity.Property(x => x.AssignedPort).HasColumnName("assigned_port").IsRequired();
            entity.Property(x => x.Enabled).HasColumnName("enabled").IsRequired();
            entity.Property(x => x.OwnerUsername).HasColumnName("owner_username").HasMaxLength(64);
            entity.Property(x => x.TargetHost).HasColumnName("target_host").HasMaxLength(255).IsRequired();
            entity.Property(x => x.TargetPort).HasColumnName("target_port").IsRequired();
            entity.Property(x => x.UpdatedAt).HasColumnName("updated_at").IsRequired();
            entity.HasOne(x => x.Owner).WithMany().HasForeignKey(x => x.OwnerUsername).OnDelete(DeleteBehavior.SetNull);
        });

        modelBuilder.Entity<BoardServiceEntity>(entity =>
        {
            entity.ToTable("board_services");
            entity.HasKey(x => new { x.BoardId, x.PublicPort });
            entity.HasIndex(x => x.PublicPort).IsUnique();
            entity.Property(x => x.BoardId).HasColumnName("board_id").HasMaxLength(64);
            entity.Property(x => x.Name).HasColumnName("name").HasMaxLength(64).IsRequired();
            entity.Property(x => x.PublicPort).HasColumnName("public_port").IsRequired();
            entity.Property(x => x.TargetHost).HasColumnName("target_host").HasMaxLength(255).IsRequired();
            entity.Property(x => x.TargetPort).HasColumnName("target_port").IsRequired();
            entity.Property(x => x.Enabled).HasColumnName("enabled").IsRequired();
            entity.HasOne(x => x.Board).WithMany(x => x.Services).HasForeignKey(x => x.BoardId).OnDelete(DeleteBehavior.Cascade);
        });

        modelBuilder.Entity<RelayEventEntity>(entity =>
        {
            entity.ToTable("relay_events");
            entity.HasKey(x => x.Id);
            entity.HasIndex(x => x.Timestamp);
            entity.Property(x => x.Id).HasColumnName("id");
            entity.Property(x => x.Timestamp).HasColumnName("timestamp").IsRequired();
            entity.Property(x => x.Level).HasColumnName("level").HasMaxLength(16).IsRequired();
            entity.Property(x => x.Message).HasColumnName("message").IsRequired();
        });
    }
}
