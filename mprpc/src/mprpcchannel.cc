#include "mprpcchannel.h"

#include "mprpcapplication.h"
#include "mprpccontroller.h"
#include "rpc_envelope.pb.h"
#include "rpcframecodec.h"
#include "zookeeperutil.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <limits>
#include <sstream>
#include <string>

namespace
{

using Clock = MprpcController::Clock;

class SocketHandle
{
public:
    explicit SocketHandle(int value = -1) : value_(value) {}
    ~SocketHandle()
    {
        if (value_ >= 0) ::close(value_);
    }
    SocketHandle(const SocketHandle &) = delete;
    SocketHandle &operator=(const SocketHandle &) = delete;
    int get() const noexcept { return value_; }

private:
    int value_;
};

class DoneGuard
{
public:
    explicit DoneGuard(google::protobuf::Closure *done) : done_(done) {}
    ~DoneGuard()
    {
        if (done_ != nullptr) done_->Run();
    }

private:
    google::protobuf::Closure *done_;
};

int positiveInt(const std::string &value, int fallback)
{
    try
    {
        const int parsed = std::stoi(value);
        return parsed > 0 ? parsed : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

std::uint32_t positiveUint(const std::string &value, std::uint32_t fallback)
{
    try
    {
        const unsigned long parsed = std::stoul(value);
        if (parsed == 0 || parsed > std::numeric_limits<std::uint32_t>::max())
            return fallback;
        return static_cast<std::uint32_t>(parsed);
    }
    catch (...)
    {
        return fallback;
    }
}

std::string nextRequestId()
{
    static std::atomic<std::uint64_t> sequence{1};
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    return std::to_string(now) + "-" +
           std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
}

int remainingMilliseconds(Clock::time_point deadline)
{
    const auto now = Clock::now();
    if (now >= deadline) return 0;
    const auto value = std::chrono::duration_cast<std::chrono::milliseconds>(
                           deadline - now)
                           .count();
    if (value <= 0) return 1;
    return static_cast<int>(std::min<std::int64_t>(
        value, std::numeric_limits<int>::max()));
}

void fail(google::protobuf::RpcController *controller,
          mprpc::RpcErrorCode code, const std::string &message)
{
    if (auto *mprpcController = dynamic_cast<MprpcController *>(controller))
    {
        mprpcController->SetFailed(code, message);
    }
    else if (controller != nullptr)
    {
        controller->SetFailed(message);
    }
}

bool cancelled(google::protobuf::RpcController *controller)
{
    return controller != nullptr && controller->IsCanceled();
}

bool waitForSocket(int fd, short events, Clock::time_point deadline,
                   google::protobuf::RpcController *controller,
                   mprpc::RpcErrorCode code, const char *operation)
{
    for (;;)
    {
        if (cancelled(controller))
        {
            fail(controller, mprpc::RpcErrorCode::Cancelled,
                 std::string(operation) + " cancelled");
            return false;
        }
        const int timeout = remainingMilliseconds(deadline);
        if (timeout <= 0)
        {
            fail(controller, mprpc::RpcErrorCode::DeadlineExceeded,
                 std::string(operation) + " deadline exceeded");
            return false;
        }
        pollfd descriptor{fd, events, 0};
        const int result = ::poll(&descriptor, 1, timeout);
        if (result > 0)
        {
            if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
            {
                fail(controller, code, std::string(operation) + " socket closed");
                return false;
            }
            return true;
        }
        if (result == 0)
        {
            fail(controller, mprpc::RpcErrorCode::DeadlineExceeded,
                 std::string(operation) + " deadline exceeded");
            return false;
        }
        if (errno != EINTR)
        {
            fail(controller, code, std::string(operation) + " poll failed");
            return false;
        }
    }
}

bool sendAll(int fd, const std::string &data, Clock::time_point deadline,
             google::protobuf::RpcController *controller)
{
    std::size_t sent = 0;
    while (sent < data.size())
    {
        if (!waitForSocket(fd, POLLOUT, deadline, controller,
                           mprpc::RpcErrorCode::SendFailed, "RPC send"))
            return false;
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif
        const ssize_t result = ::send(fd, data.data() + sent,
                                      data.size() - sent, flags);
        if (result > 0)
        {
            sent += static_cast<std::size_t>(result);
            continue;
        }
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            continue;
        fail(controller, mprpc::RpcErrorCode::SendFailed, "RPC send failed");
        return false;
    }
    return true;
}

bool receiveExact(int fd, char *destination, std::size_t size,
                  Clock::time_point deadline,
                  google::protobuf::RpcController *controller)
{
    std::size_t received = 0;
    while (received < size)
    {
        if (!waitForSocket(fd, POLLIN, deadline, controller,
                           mprpc::RpcErrorCode::ReceiveFailed, "RPC receive"))
            return false;
        const ssize_t result = ::recv(fd, destination + received,
                                      size - received, 0);
        if (result > 0)
        {
            received += static_cast<std::size_t>(result);
            continue;
        }
        if (result == 0)
        {
            fail(controller, mprpc::RpcErrorCode::ReceiveFailed,
                 "RPC peer closed before returning a complete frame");
            return false;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
        fail(controller, mprpc::RpcErrorCode::ReceiveFailed, "RPC receive failed");
        return false;
    }
    return true;
}

bool parseAddress(const std::string &address, std::string &ip,
                  std::uint16_t &port)
{
    const auto separator = address.rfind(':');
    if (separator == std::string::npos || separator == 0 ||
        separator + 1 >= address.size())
        return false;
    ip = address.substr(0, separator);
    try
    {
        const unsigned long parsed = std::stoul(address.substr(separator + 1));
        if (parsed == 0 || parsed > 65535) return false;
        port = static_cast<std::uint16_t>(parsed);
    }
    catch (...)
    {
        return false;
    }
    return true;
}

mprpc::RpcErrorCode remoteError(mprpc::RpcStatusCode status)
{
    if (status == mprpc::RPC_STATUS_DEADLINE_EXCEEDED)
        return mprpc::RpcErrorCode::DeadlineExceeded;
    if (status == mprpc::RPC_STATUS_SERVER_BUSY)
        return mprpc::RpcErrorCode::ServerBusy;
    return mprpc::RpcErrorCode::RemoteError;
}

} // namespace

void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
                              google::protobuf::RpcController *controller,
                              const google::protobuf::Message *request,
                              google::protobuf::Message *response,
                              google::protobuf::Closure *done)
{
    DoneGuard doneGuard(done);
    MprpcController fallbackController;
    if (controller == nullptr) controller = &fallbackController;

    const auto &config = MprpcApplication::GetInstance().GetConfig();
    auto *mprpcController = dynamic_cast<MprpcController *>(controller);
    Clock::time_point deadline = mprpcController == nullptr
                                     ? Clock::time_point::max()
                                     : mprpcController->Deadline();
    if (deadline == Clock::time_point::max())
    {
        deadline = Clock::now() + std::chrono::milliseconds(positiveInt(
                                      config.LoadOr("rpcdeadline_ms", "2000"),
                                      2000));
        if (mprpcController != nullptr) mprpcController->SetDeadline(deadline);
    }
    if (remainingMilliseconds(deadline) <= 0)
    {
        fail(controller, mprpc::RpcErrorCode::DeadlineExceeded,
             "RPC deadline already expired");
        return;
    }

    std::string requestPayload;
    if (request == nullptr || !request->SerializeToString(&requestPayload))
    {
        fail(controller, mprpc::RpcErrorCode::ProtocolError,
             "failed to serialize RPC request");
        return;
    }

    const std::string serviceName = method->service()->full_name();
    const std::string methodName = method->name();
    const std::string requestId = nextRequestId();

    const int remainingForDiscovery = remainingMilliseconds(deadline);
    const int configuredZkTimeout = positiveInt(
        config.LoadOr("zkconnecttimeout_ms", "1000"), 1000);
    ZkClient zkClient;
    std::string discoveryError;
    if (!zkClient.Start(std::chrono::milliseconds(
                            std::min(remainingForDiscovery, configuredZkTimeout)),
                        &discoveryError))
    {
        fail(controller,
             remainingMilliseconds(deadline) <= 0
                 ? mprpc::RpcErrorCode::DeadlineExceeded
                 : mprpc::RpcErrorCode::DiscoveryUnavailable,
             discoveryError);
        return;
    }

    std::string address;
    const std::string methodPath = "/" + serviceName + "/" + methodName;
    const int remainingForQuery = remainingMilliseconds(deadline);
    if (remainingForQuery <= 0 ||
        !zkClient.GetData(methodPath.c_str(), address,
                          std::chrono::milliseconds(remainingForQuery),
                          &discoveryError))
    {
        fail(controller,
             remainingMilliseconds(deadline) <= 0 ||
                     discoveryError.find("timed out") != std::string::npos
                 ? mprpc::RpcErrorCode::DeadlineExceeded
                 : mprpc::RpcErrorCode::ServiceNotFound,
             methodPath + " is unavailable: " + discoveryError);
        return;
    }
    if (remainingMilliseconds(deadline) <= 0)
    {
        fail(controller, mprpc::RpcErrorCode::DeadlineExceeded,
             "RPC deadline exceeded during service discovery");
        return;
    }

    std::string ip;
    std::uint16_t port = 0;
    if (!parseAddress(address, ip, port))
    {
        fail(controller, mprpc::RpcErrorCode::InvalidAddress,
             "invalid RPC endpoint: " + address);
        return;
    }

    mprpc::RpcEnvelope envelope;
    envelope.set_protocol_version(1);
    envelope.set_message_type(mprpc::RPC_MESSAGE_TYPE_REQUEST);
    envelope.set_request_id(requestId);
    envelope.set_service_name(serviceName);
    envelope.set_method_name(methodName);
    envelope.set_payload(std::move(requestPayload));
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - Clock::now());
    const auto wireDeadline = std::chrono::system_clock::now() +
                              std::max(remaining, std::chrono::milliseconds(0));
    envelope.set_deadline_unix_ms(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            wireDeadline.time_since_epoch())
            .count());

    std::string serializedEnvelope;
    if (!envelope.SerializeToString(&serializedEnvelope))
    {
        fail(controller, mprpc::RpcErrorCode::ProtocolError,
             "failed to serialize RPC envelope");
        return;
    }
    const std::uint32_t maxFrameLength = positiveUint(
        config.LoadOr("rpcmaxframe_bytes", "4194304"),
        mprpc::RpcFrameCodec::kDefaultMaxFrameLength);
    std::string outboundFrame;
    try
    {
        outboundFrame = mprpc::RpcFrameCodec::encode(serializedEnvelope,
                                                     maxFrameLength);
    }
    catch (const std::exception &error)
    {
        fail(controller, mprpc::RpcErrorCode::ProtocolError, error.what());
        return;
    }

    SocketHandle socketHandle(::socket(AF_INET, SOCK_STREAM, 0));
    if (socketHandle.get() < 0)
    {
        fail(controller, mprpc::RpcErrorCode::ConnectFailed,
             "failed to create RPC socket");
        return;
    }
    const int existingFlags = ::fcntl(socketHandle.get(), F_GETFL, 0);
    if (existingFlags < 0 ||
        ::fcntl(socketHandle.get(), F_SETFL, existingFlags | O_NONBLOCK) < 0)
    {
        fail(controller, mprpc::RpcErrorCode::ConnectFailed,
             "failed to configure nonblocking RPC socket");
        return;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip.c_str(), &serverAddress.sin_addr) != 1)
    {
        fail(controller, mprpc::RpcErrorCode::InvalidAddress,
             "RPC endpoint must contain a valid IPv4 address");
        return;
    }

    const int connectResult = ::connect(
        socketHandle.get(), reinterpret_cast<sockaddr *>(&serverAddress),
        sizeof(serverAddress));
    if (connectResult < 0 && errno != EINPROGRESS)
    {
        fail(controller, mprpc::RpcErrorCode::ConnectFailed,
             "RPC connect failed");
        return;
    }
    if (connectResult < 0)
    {
        if (!waitForSocket(socketHandle.get(), POLLOUT, deadline, controller,
                           mprpc::RpcErrorCode::ConnectFailed, "RPC connect"))
            return;
        int socketError = 0;
        socklen_t socketErrorLength = sizeof(socketError);
        if (::getsockopt(socketHandle.get(), SOL_SOCKET, SO_ERROR, &socketError,
                         &socketErrorLength) < 0 || socketError != 0)
        {
            fail(controller, mprpc::RpcErrorCode::ConnectFailed,
                 "RPC connect failed");
            return;
        }
    }

    if (!sendAll(socketHandle.get(), outboundFrame, deadline, controller)) return;

    char header[mprpc::RpcFrameCodec::kHeaderLength]{};
    if (!receiveExact(socketHandle.get(), header, sizeof(header), deadline,
                      controller))
        return;
    const auto *bytes = reinterpret_cast<const unsigned char *>(header);
    const std::uint32_t responseLength =
        (static_cast<std::uint32_t>(bytes[0]) << 24U) |
        (static_cast<std::uint32_t>(bytes[1]) << 16U) |
        (static_cast<std::uint32_t>(bytes[2]) << 8U) |
        static_cast<std::uint32_t>(bytes[3]);
    if (responseLength == 0 || responseLength > maxFrameLength)
    {
        fail(controller, mprpc::RpcErrorCode::ProtocolError,
             "invalid RPC response frame length");
        return;
    }
    std::string responseFrame(responseLength, '\0');
    if (!receiveExact(socketHandle.get(), responseFrame.data(),
                      responseFrame.size(), deadline, controller))
        return;

    mprpc::RpcEnvelope responseEnvelope;
    if (!responseEnvelope.ParseFromString(responseFrame) ||
        responseEnvelope.protocol_version() != 1 ||
        responseEnvelope.message_type() != mprpc::RPC_MESSAGE_TYPE_RESPONSE)
    {
        fail(controller, mprpc::RpcErrorCode::ProtocolError,
             "invalid RPC response envelope");
        return;
    }
    if (responseEnvelope.status_code() != mprpc::RPC_STATUS_OK)
    {
        if (!responseEnvelope.request_id().empty() &&
            responseEnvelope.request_id() != requestId)
        {
            fail(controller, mprpc::RpcErrorCode::ProtocolError,
                 "mismatched RPC error response");
            return;
        }
        fail(controller, remoteError(responseEnvelope.status_code()),
             responseEnvelope.error_message());
        return;
    }
    if (responseEnvelope.request_id() != requestId)
    {
        fail(controller, mprpc::RpcErrorCode::ProtocolError,
             "mismatched RPC success response");
        return;
    }
    if (response == nullptr ||
        !response->ParseFromString(responseEnvelope.payload()))
    {
        fail(controller, mprpc::RpcErrorCode::ProtocolError,
             "failed to parse RPC response payload");
    }
}
