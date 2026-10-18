#include "rpcprovider.h"

#include "logger.h"
#include "mprpcapplication.h"
#include "mprpccontroller.h"
#include "rpc_envelope.pb.h"
#include "zookeeperutil.h"

#include <muduo/net/EventLoop.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{

std::size_t positiveSize(const std::string &value, std::size_t fallback)
{
    try
    {
        const unsigned long long parsed = std::stoull(value);
        return parsed == 0 ? fallback : static_cast<std::size_t>(parsed);
    }
    catch (...)
    {
        return fallback;
    }
}

std::uint32_t positiveUint(const std::string &value, std::uint32_t fallback)
{
    const std::size_t parsed = positiveSize(value, fallback);
    return parsed > std::numeric_limits<std::uint32_t>::max()
               ? fallback
               : static_cast<std::uint32_t>(parsed);
}

int positiveInt(const std::string &value, int fallback)
{
    return static_cast<int>(std::min<std::size_t>(
        positiveSize(value, static_cast<std::size_t>(fallback)),
        static_cast<std::size_t>(std::numeric_limits<int>::max())));
}

bool deadlineExpired(std::int64_t deadlineUnixMs)
{
    if (deadlineUnixMs <= 0) return true;
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    return now >= deadlineUnixMs;
}

class RpcDoneClosure final : public google::protobuf::Closure
{
public:
    explicit RpcDoneClosure(std::function<void()> callback)
        : callback_(std::move(callback)) {}

    void Run() override
    {
        auto callback = std::move(callback_);
        delete this;
        if (callback) callback();
    }

private:
    std::function<void()> callback_;
};

} // namespace

struct RpcProvider::CallContext
{
    std::weak_ptr<muduo::net::TcpConnection> connection;
    std::string requestId;
    std::int64_t deadlineUnixMs{0};
    std::unique_ptr<google::protobuf::Message> request;
    std::unique_ptr<google::protobuf::Message> response;
    MprpcController controller;
};

RpcProvider::RpcProvider()
    : executor_(
          "rpc-provider",
          positiveSize(MprpcApplication::GetInstance().GetConfig().LoadOr(
                           "providerthreads", "4"),
                       4),
          positiveSize(MprpcApplication::GetInstance().GetConfig().LoadOr(
                           "providerqueue", "512"),
                       512)),
      codec_(positiveUint(MprpcApplication::GetInstance().GetConfig().LoadOr(
                              "rpcmaxframe_bytes", "4194304"),
                          mprpc::RpcFrameCodec::kDefaultMaxFrameLength))
{
}

void RpcProvider::NotifyService(google::protobuf::Service *service)
{
    if (service == nullptr) throw std::invalid_argument("RPC service is null");
    ServiceInfo serviceInfo;
    const auto *descriptor = service->GetDescriptor();
    const std::string serviceName = descriptor->full_name();
    for (int index = 0; index < descriptor->method_count(); ++index)
    {
        const auto *method = descriptor->method(index);
        serviceInfo.m_methodMap.emplace(method->name(), method);
        LOG_INFO("rpc method registered: %s/%s", serviceName.c_str(),
                 method->name().c_str());
    }
    serviceInfo.m_service = service;
    m_serviceMap[serviceName] = std::move(serviceInfo);
}

bool RpcProvider::Run()
{
    auto &config = MprpcApplication::GetInstance().GetConfig();
    const std::string ip = config.LoadOr("rpcserverip", "127.0.0.1");
    const int portValue = positiveInt(config.LoadOr("rpcserverport", "8001"),
                                      8001);
    if (portValue <= 0 || portValue > 65535)
    {
        LOG_ERR("invalid RPC provider port");
        return false;
    }

    muduo::net::InetAddress address(ip, static_cast<std::uint16_t>(portValue));
    muduo::net::TcpServer server(&m_eventLoop, address, "NexusIM-RpcProvider");
    server.setConnectionCallback(
        std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
    server.setMessageCallback(
        std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3));
    server.setThreadNum(positiveInt(
        config.LoadOr("provideriothreads", "2"), 2));
    server.start();

    ZkClient zkClient;
    std::string zkError;
    const int connectTimeout = positiveInt(
        config.LoadOr("zkconnecttimeout_ms", "2000"), 2000);
    if (!zkClient.Start(std::chrono::milliseconds(connectTimeout), &zkError))
    {
        LOG_ERR("ZooKeeper registration failed: %s", zkError.c_str());
        return false;
    }

    for (const auto &serviceEntry : m_serviceMap)
    {
        const std::string servicePath = "/" + serviceEntry.first;
        if (!zkClient.Create(servicePath.c_str(), nullptr, 0, 0, &zkError))
        {
            LOG_ERR("service znode registration failed: %s", zkError.c_str());
            return false;
        }
        for (const auto &methodEntry : serviceEntry.second.m_methodMap)
        {
            const std::string methodPath = servicePath + "/" + methodEntry.first;
            const std::string endpoint = ip + ":" + std::to_string(portValue);
            if (!zkClient.Create(methodPath.c_str(), endpoint.data(),
                                 static_cast<int>(endpoint.size()),
                                 ZOO_EPHEMERAL, &zkError))
            {
                LOG_ERR("method znode registration failed: %s", zkError.c_str());
                return false;
            }
        }
    }

    LOG_INFO("RpcProvider listening on %s:%d", ip.c_str(), portValue);
    m_eventLoop.loop();
    return true;
}

void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &connection)
{
    if (!connection->connected())
    {
        LOG_INFO("RPC connection closed: %s", connection->name().c_str());
    }
}

void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &connection,
                            muduo::net::Buffer *buffer, muduo::Timestamp)
{
    const auto result = codec_.decode(buffer, [this, connection](std::string frame) {
        if (!executor_.trySubmit(
                [this, connection, frame = std::move(frame)]() mutable {
                    DispatchFrame(connection, std::move(frame));
                }))
        {
            SendError(connection, "", mprpc::RPC_STATUS_SERVER_BUSY,
                      "RPC provider queue is full", false);
        }
    });
    if (result.status == mprpc::RpcFrameCodec::Status::InvalidLength)
    {
        SendError(connection, "", mprpc::RPC_STATUS_INVALID_REQUEST,
                  "invalid or oversized RPC frame", true);
    }
}

void RpcProvider::DispatchFrame(const muduo::net::TcpConnectionPtr &connection,
                                std::string frame)
{
    mprpc::RpcEnvelope envelope;
    if (!envelope.ParseFromString(frame) || envelope.protocol_version() != 1 ||
        envelope.message_type() != mprpc::RPC_MESSAGE_TYPE_REQUEST ||
        envelope.request_id().empty())
    {
        SendError(connection, envelope.request_id(),
                  mprpc::RPC_STATUS_INVALID_REQUEST,
                  "invalid RPC request envelope", false);
        return;
    }
    if (deadlineExpired(envelope.deadline_unix_ms()))
    {
        SendError(connection, envelope.request_id(),
                  mprpc::RPC_STATUS_DEADLINE_EXCEEDED,
                  "RPC request deadline exceeded", false);
        return;
    }

    const auto serviceIterator = m_serviceMap.find(envelope.service_name());
    if (serviceIterator == m_serviceMap.end())
    {
        SendError(connection, envelope.request_id(),
                  mprpc::RPC_STATUS_SERVICE_NOT_FOUND,
                  "RPC service is not registered", false);
        return;
    }
    const auto methodIterator =
        serviceIterator->second.m_methodMap.find(envelope.method_name());
    if (methodIterator == serviceIterator->second.m_methodMap.end())
    {
        SendError(connection, envelope.request_id(),
                  mprpc::RPC_STATUS_METHOD_NOT_FOUND,
                  "RPC method is not registered", false);
        return;
    }

    auto context = std::make_shared<CallContext>();
    context->connection = connection;
    context->requestId = envelope.request_id();
    context->deadlineUnixMs = envelope.deadline_unix_ms();
    const auto remaining = std::chrono::milliseconds(
        std::max<std::int64_t>(
            0, envelope.deadline_unix_ms() -
                   std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count()));
    context->controller.SetDeadline(MprpcController::Clock::now() + remaining);
    auto *service = serviceIterator->second.m_service;
    const auto *method = methodIterator->second;
    context->request.reset(service->GetRequestPrototype(method).New());
    context->response.reset(service->GetResponsePrototype(method).New());
    if (!context->request->ParseFromString(envelope.payload()))
    {
        SendError(connection, envelope.request_id(),
                  mprpc::RPC_STATUS_PARSE_ERROR,
                  "RPC request payload could not be parsed", false);
        return;
    }

    auto *done = new RpcDoneClosure(
        [this, context] { SendRpcResponse(context); });
    try
    {
        service->CallMethod(method, &context->controller,
                            context->request.get(), context->response.get(), done);
    }
    catch (const std::exception &error)
    {
        context->controller.SetFailed(mprpc::RpcErrorCode::InternalError,
                                      error.what());
        done->Run();
    }
    catch (...)
    {
        context->controller.SetFailed(mprpc::RpcErrorCode::InternalError,
                                      "unhandled RPC service exception");
        done->Run();
    }
}

void RpcProvider::SendRpcResponse(const std::shared_ptr<CallContext> &context)
{
    const auto connection = context->connection.lock();
    if (!connection || !connection->connected()) return;
    if (deadlineExpired(context->deadlineUnixMs))
    {
        SendError(connection, context->requestId,
                  mprpc::RPC_STATUS_DEADLINE_EXCEEDED,
                  "RPC service completed after the deadline", false);
        return;
    }
    if (context->controller.Failed())
    {
        SendError(connection, context->requestId,
                  mprpc::RPC_STATUS_SERVICE_ERROR,
                  context->controller.ErrorText(), false);
        return;
    }

    std::string responsePayload;
    if (!context->response->SerializeToString(&responsePayload))
    {
        SendError(connection, context->requestId,
                  mprpc::RPC_STATUS_INTERNAL_ERROR,
                  "RPC response serialization failed", false);
        return;
    }

    mprpc::RpcEnvelope envelope;
    envelope.set_protocol_version(1);
    envelope.set_message_type(mprpc::RPC_MESSAGE_TYPE_RESPONSE);
    envelope.set_request_id(context->requestId);
    envelope.set_status_code(mprpc::RPC_STATUS_OK);
    envelope.set_payload(std::move(responsePayload));
    std::string payload;
    if (!envelope.SerializeToString(&payload))
    {
        SendError(connection, context->requestId,
                  mprpc::RPC_STATUS_INTERNAL_ERROR,
                  "RPC envelope serialization failed", false);
        return;
    }

    std::string frame;
    try
    {
        frame = mprpc::RpcFrameCodec::encode(payload, codec_.maxFrameLength());
    }
    catch (const std::exception &error)
    {
        SendError(connection, context->requestId,
                  mprpc::RPC_STATUS_INTERNAL_ERROR, error.what(), false);
        return;
    }
    std::weak_ptr<muduo::net::TcpConnection> weakConnection(connection);
    connection->getLoop()->queueInLoop(
        [weakConnection, frame = std::move(frame)] {
            const auto liveConnection = weakConnection.lock();
            if (liveConnection && liveConnection->connected())
                liveConnection->send(frame);
        });
}

void RpcProvider::SendError(const muduo::net::TcpConnectionPtr &connection,
                            const std::string &requestId, int statusCode,
                            const std::string &message, bool closeAfter)
{
    if (!connection) return;
    mprpc::RpcEnvelope envelope;
    envelope.set_protocol_version(1);
    envelope.set_message_type(mprpc::RPC_MESSAGE_TYPE_RESPONSE);
    envelope.set_request_id(requestId);
    envelope.set_status_code(static_cast<mprpc::RpcStatusCode>(statusCode));
    envelope.set_error_message(message);
    std::string payload;
    if (!envelope.SerializeToString(&payload)) return;

    std::string frame;
    try
    {
        frame = mprpc::RpcFrameCodec::encode(payload, codec_.maxFrameLength());
    }
    catch (...)
    {
        closeAfter = true;
    }
    std::weak_ptr<muduo::net::TcpConnection> weakConnection(connection);
    connection->getLoop()->queueInLoop(
        [weakConnection, frame = std::move(frame), closeAfter] {
            const auto liveConnection = weakConnection.lock();
            if (!liveConnection || !liveConnection->connected()) return;
            if (!frame.empty()) liveConnection->send(frame);
            if (closeAfter) liveConnection->shutdown();
        });
}
