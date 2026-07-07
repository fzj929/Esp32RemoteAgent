namespace RelayServer.Models;

public sealed record LoginRequest(string Username, string Password);

public sealed record ChangePasswordRequest(string CurrentPassword, string NewPassword);

public sealed record CreateUserRequest(string Username, string Password, string Role);

public sealed record ResetPasswordRequest(string NewPassword);

public sealed record UpdateUserRequest(string Role);
