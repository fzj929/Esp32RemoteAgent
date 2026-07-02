using RelayServer.Models;
using RelayServer.Options;

namespace RelayServer.Validation;

public static class BoardValidator
{
    public static string? Validate(BoardEditRequest request, RelayOptions options)
    {
        if (string.IsNullOrWhiteSpace(request.BoardId))
        {
            return "BoardId is required.";
        }

        if (string.IsNullOrWhiteSpace(request.AuthKey))
        {
            return "AuthKey is required.";
        }

        if (request.AssignedPort < options.PublicPortMin || request.AssignedPort > options.PublicPortMax)
        {
            return $"AssignedPort must be between {options.PublicPortMin} and {options.PublicPortMax}.";
        }

        if (options.ReservedPorts.Contains(request.AssignedPort))
        {
            return $"Port {request.AssignedPort} is reserved.";
        }

        if (string.IsNullOrWhiteSpace(request.TargetHost))
        {
            return "TargetHost is required.";
        }

        if (request.TargetPort <= 0 || request.TargetPort > 65535)
        {
            return "TargetPort is invalid.";
        }

        return null;
    }
}
