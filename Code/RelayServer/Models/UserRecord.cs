namespace RelayServer.Models;

public static class UserRoles
{
    public const string Administrator = "Administrator";
    public const string User = "User";
}

public sealed record UserRecord(
    string Username,
    string PasswordHash,
    string Role,
    DateTimeOffset UpdatedAt);

public sealed record UserDto(
    string Username,
    string Role,
    DateTimeOffset UpdatedAt)
{
    public static UserDto From(UserRecord user) =>
        new(user.Username, user.Role, user.UpdatedAt);
}
