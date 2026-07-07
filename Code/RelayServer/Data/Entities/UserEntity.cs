namespace RelayServer.Data.Entities;

public sealed class UserEntity
{
    public string Username { get; set; } = string.Empty;
    public string PasswordHash { get; set; } = string.Empty;
    public string Role { get; set; } = string.Empty;
    public DateTimeOffset UpdatedAt { get; set; }
}
