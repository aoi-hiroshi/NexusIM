#include "rpc_envelope.pb.h"
#include "rpcframecodec.h"
#include "user_service.pb.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <thread>

namespace
{

class Socket
{
public:
    explicit Socket(int value) : value_(value) {}
    ~Socket() { if (value_ >= 0) ::close(value_); }
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    Socket(Socket &&other) noexcept : value_(other.value_)
    {
        other.value_ = -1;
    }
    int get() const { return value_; }
private:
    int value_;
};

void require(bool condition, const std::string &message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << std::endl;
        std::exit(1);
    }
}

Socket connectTo(const std::string &host, std::uint16_t port)
{
    const int descriptor = ::socket(AF_INET, SOCK_STREAM, 0);
    require(descriptor >= 0, "socket creation failed");
    timeval timeout{5, 0};
    ::setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(descriptor, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    require(::inet_pton(AF_INET, host.c_str(), &address.sin_addr) == 1,
            "invalid IPv4 host");
    require(::connect(descriptor, reinterpret_cast<sockaddr *>(&address),
                      sizeof(address)) == 0,
            "connect failed");
    return Socket(descriptor);
}

void sendAll(int descriptor, const std::string &data)
{
    std::size_t sent = 0;
    while (sent < data.size())
    {
        const ssize_t count = ::send(descriptor, data.data() + sent,
                                     data.size() - sent, 0);
        require(count > 0, "send failed");
        sent += static_cast<std::size_t>(count);
    }
}

std::string receiveExact(int descriptor, std::size_t size)
{
    std::string result(size, '\0');
    std::size_t received = 0;
    while (received < size)
    {
        const ssize_t count = ::recv(descriptor, result.data() + received,
                                     size - received, 0);
        require(count > 0, "receive failed or peer closed early");
        received += static_cast<std::size_t>(count);
    }
    return result;
}

mprpc::RpcEnvelope receiveEnvelope(int descriptor)
{
    const std::string header = receiveExact(descriptor, 4);
    const auto *bytes = reinterpret_cast<const unsigned char *>(header.data());
    const std::uint32_t length =
        (static_cast<std::uint32_t>(bytes[0]) << 24U) |
        (static_cast<std::uint32_t>(bytes[1]) << 16U) |
        (static_cast<std::uint32_t>(bytes[2]) << 8U) |
        static_cast<std::uint32_t>(bytes[3]);
    require(length > 0 && length <= mprpc::RpcFrameCodec::kDefaultMaxFrameLength,
            "invalid response frame length");
    mprpc::RpcEnvelope response;
    require(response.ParseFromString(receiveExact(descriptor, length)),
            "response envelope parse failed");
    return response;
}

std::string queryFrame(const std::string &requestId, int userId)
{
    nexusim::user::QueryUserRequest request;
    request.set_user_id(userId);
    std::string requestPayload;
    require(request.SerializeToString(&requestPayload), "request serialize failed");

    mprpc::RpcEnvelope envelope;
    envelope.set_protocol_version(1);
    envelope.set_message_type(mprpc::RPC_MESSAGE_TYPE_REQUEST);
    envelope.set_request_id(requestId);
    envelope.set_service_name("nexusim.user.UserService");
    envelope.set_method_name("QueryUser");
    envelope.set_payload(requestPayload);
    envelope.set_deadline_unix_ms(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            (std::chrono::system_clock::now() + std::chrono::seconds(5))
                .time_since_epoch())
            .count());
    std::string serialized;
    require(envelope.SerializeToString(&serialized), "envelope serialize failed");
    return mprpc::RpcFrameCodec::encode(serialized);
}

void testHalfFrame(const std::string &host, std::uint16_t port, int userId)
{
    auto socket = connectTo(host, port);
    const std::string frame = queryFrame("half", userId);
    sendAll(socket.get(), frame.substr(0, 2));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    sendAll(socket.get(), frame.substr(2));
    const auto response = receiveEnvelope(socket.get());
    require(response.request_id() == "half" &&
                response.status_code() == mprpc::RPC_STATUS_OK,
            "half-frame request did not complete normally");
}

void testStickyFrames(const std::string &host, std::uint16_t port, int userId)
{
    auto socket = connectTo(host, port);
    sendAll(socket.get(), queryFrame("sticky-1", userId) +
                              queryFrame("sticky-2", userId));
    std::set<std::string> requestIds;
    const auto first = receiveEnvelope(socket.get());
    const auto second = receiveEnvelope(socket.get());
    require(first.status_code() == mprpc::RPC_STATUS_OK &&
                second.status_code() == mprpc::RPC_STATUS_OK,
            "sticky requests did not complete normally");
    requestIds.insert(first.request_id());
    requestIds.insert(second.request_id());
    require(requestIds == std::set<std::string>({"sticky-1", "sticky-2"}),
            "sticky request frames were not both answered");
}

void testMalformedEnvelope(const std::string &host, std::uint16_t port)
{
    auto socket = connectTo(host, port);
    sendAll(socket.get(), mprpc::RpcFrameCodec::encode(
                              std::string("\x0a\xff", 2)));
    const auto response = receiveEnvelope(socket.get());
    require(response.status_code() == mprpc::RPC_STATUS_INVALID_REQUEST,
            "malformed protobuf envelope was not rejected");
}

void testIllegalLength(const std::string &host, std::uint16_t port)
{
    auto socket = connectTo(host, port);
    sendAll(socket.get(), std::string(4, '\0'));
    const auto response = receiveEnvelope(socket.get());
    require(response.status_code() == mprpc::RPC_STATUS_INVALID_REQUEST,
            "zero RPC frame length was not rejected");
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::cerr << "usage: stage3_rpc_transport_probe <host> <port> <user_id>"
                  << std::endl;
        return 2;
    }
    const std::string host = argv[1];
    const unsigned long parsedPort = std::stoul(argv[2]);
    require(parsedPort > 0 && parsedPort <= 65535, "invalid port");
    const auto port = static_cast<std::uint16_t>(parsedPort);
    const int userId = std::stoi(argv[3]);
    testHalfFrame(host, port, userId);
    testStickyFrames(host, port, userId);
    testMalformedEnvelope(host, port);
    testIllegalLength(host, port);
    std::cout << "stage3_rpc_transport_probe: PASS" << std::endl;
    return 0;
}
