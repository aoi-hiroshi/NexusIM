#pragma once

namespace mprpc
{

enum class RpcErrorCode
{
    Ok = 0,
    Cancelled,
    DeadlineExceeded,
    DiscoveryUnavailable,
    ServiceNotFound,
    InvalidAddress,
    ConnectFailed,
    SendFailed,
    ReceiveFailed,
    ProtocolError,
    RemoteError,
    ServerBusy,
    InternalError
};

} // namespace mprpc
