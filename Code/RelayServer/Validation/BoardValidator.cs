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

        if (string.Equals(request.AuthKey.Trim(), "CHANGE_THIS_DEVICE_SECRET", StringComparison.Ordinal))
        {
            return "AuthKey must be changed from the default placeholder.";
        }

        var services = NormalizeServices(request);
        if (services.Count == 0)
        {
            return "At least one TCP service is required.";
        }

        foreach (var service in services)
        {
            if (string.IsNullOrWhiteSpace(service.Name))
            {
                return "Service name is required.";
            }

            if (service.PublicPort < options.PublicPortMin || service.PublicPort > options.PublicPortMax)
            {
                return $"PublicPort must be between {options.PublicPortMin} and {options.PublicPortMax}.";
            }

            if (options.ReservedPorts.Contains(service.PublicPort))
            {
                return $"Port {service.PublicPort} is reserved.";
            }

            if (string.IsNullOrWhiteSpace(service.TargetHost))
            {
                return "TargetHost is required.";
            }

            if (service.TargetPort <= 0 || service.TargetPort > 65535)
            {
                return "TargetPort is invalid.";
            }
        }

        if (services.Select(x => x.PublicPort).Distinct().Count() != services.Count)
        {
            return "Public service ports must be unique.";
        }

        return null;
    }

    public static IReadOnlyList<BoardServiceEditRequest> NormalizeServices(BoardEditRequest request)
    {
        if (request.Services is { Count: > 0 })
        {
            return request.Services
                .Select(x => new BoardServiceEditRequest(
                    x.Name.Trim(),
                    x.PublicPort,
                    x.TargetHost.Trim(),
                    x.TargetPort,
                    x.Enabled))
                .ToList();
        }

        return
        [
            new BoardServiceEditRequest("RDP", request.AssignedPort, request.TargetHost.Trim(), request.TargetPort, true)
        ];
    }
}
