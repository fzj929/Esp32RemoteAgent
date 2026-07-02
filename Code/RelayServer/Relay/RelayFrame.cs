using System.Buffers.Binary;
using System.Net.Sockets;
using System.Text.Json;

namespace RelayServer.Relay;

public sealed record RelayFrame(RelayFrameType Type, uint ConnectionId, ReadOnlyMemory<byte> Payload)
{
    public static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web)
    {
        PropertyNameCaseInsensitive = true
    };

    public static async Task<RelayFrame?> ReadAsync(NetworkStream stream, CancellationToken token)
    {
        var header = new byte[9];
        if (!await ReadExactAsync(stream, header, token))
        {
            return null;
        }

        var type = (RelayFrameType)header[0];
        var connectionId = BinaryPrimitives.ReadUInt32BigEndian(header.AsSpan(1, 4));
        var length = BinaryPrimitives.ReadUInt32BigEndian(header.AsSpan(5, 4));
        if (length > 1024 * 1024)
        {
            throw new InvalidOperationException("Frame payload is too large.");
        }

        var payload = new byte[length];
        if (length > 0 && !await ReadExactAsync(stream, payload, token))
        {
            return null;
        }

        return new RelayFrame(type, connectionId, payload);
    }

    public static async Task WriteJsonAsync<T>(
        NetworkStream stream,
        RelayFrameType type,
        uint connectionId,
        T payload,
        CancellationToken token,
        SemaphoreSlim? writeLock = null)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(payload, JsonOptions);
        await WriteAsync(stream, type, connectionId, bytes, token, writeLock);
    }

    public static async Task WriteAsync(
        NetworkStream stream,
        RelayFrameType type,
        uint connectionId,
        ReadOnlyMemory<byte> payload,
        CancellationToken token,
        SemaphoreSlim? writeLock = null)
    {
        if (writeLock is not null)
        {
            await writeLock.WaitAsync(token);
        }

        try
        {
            var header = new byte[9];
            header[0] = (byte)type;
            BinaryPrimitives.WriteUInt32BigEndian(header.AsSpan(1, 4), connectionId);
            BinaryPrimitives.WriteUInt32BigEndian(header.AsSpan(5, 4), (uint)payload.Length);
            await stream.WriteAsync(header, token);
            if (!payload.IsEmpty)
            {
                await stream.WriteAsync(payload, token);
            }
        }
        finally
        {
            writeLock?.Release();
        }
    }

    private static async Task<bool> ReadExactAsync(NetworkStream stream, Memory<byte> buffer, CancellationToken token)
    {
        var offset = 0;
        while (offset < buffer.Length)
        {
            var read = await stream.ReadAsync(buffer[offset..], token);
            if (read <= 0)
            {
                return false;
            }
            offset += read;
        }

        return true;
    }
}
