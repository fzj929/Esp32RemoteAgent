namespace RelayServer.Relay;

public enum RelayFrameType : byte
{
    Register = 1,
    RegisterAck = 2,
    Heartbeat = 3,
    Open = 4,
    Data = 5,
    Close = 6,
    Error = 7
}
