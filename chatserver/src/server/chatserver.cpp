#include "server/chatserver.hpp"

#include "json.hpp"
#include "public.hpp"
#include "server/chatservice.hpp"
#include "server/wireprotocol.hpp"

#include <muduo/base/Logging.h>

#include <chrono>
#include <memory>
#include <utility>

using json = nlohmann::json;

ChatServer::ChatServer(muduo::net::EventLoop *loop,
                       const muduo::net::InetAddress &listenAddress,
                       const std::string &name)
    : server_(loop, listenAddress, name), loop_(loop),
      service_(ChatService::instance()), codec_(),
      businessStrand_(4, 1024, 64),
      presenceExecutor_("presence", 2, 512), nextRequestId_(1)
{
    server_.setConnectionCallback(
        std::bind(&ChatServer::onConnection, this, std::placeholders::_1));
    server_.setMessageCallback(
        std::bind(&ChatServer::onMessage, this, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3));
    server_.setThreadNum(4);
    loop_->runEvery(1.0, [this] { pendingRequests_.expire(); });
}

void ChatServer::start()
{
    server_.start();
}

void ChatServer::onConnection(const muduo::net::TcpConnectionPtr &connection)
{
    if (connection->connected())
    {
        service_->connectionOpened(connection);
        return;
    }

    const auto session = service_->detachConnection(connection);
    businessStrand_.cancel(connection->name());
    if (session)
    {
        pendingRequests_.cancelConnection(session->connectionId, session->sessionId);
        if (session->authenticated &&
            !presenceExecutor_.trySubmit([service = service_, session = *session] {
                service->cleanupSession(session);
            }))
        {
            LOG_WARN << "presence cleanup queue full; Redis TTL will expire session "
                     << session->sessionId;
        }
    }
}

void ChatServer::onMessage(const muduo::net::TcpConnectionPtr &connection,
                           muduo::net::Buffer *buffer, muduo::Timestamp timestamp)
{
    const auto result = codec_.decode(buffer, [this, connection, timestamp](std::string frame) {
        dispatchFrame(connection, std::move(frame), timestamp);
    });
    if (result.status == nexusim::FrameCodec::Status::InvalidLength)
    {
        sendProtocolError(connection, "INVALID_FRAME", "invalid or oversized frame length");
        connection->shutdown();
    }
}

void ChatServer::dispatchFrame(const muduo::net::TcpConnectionPtr &connection,
                               std::string frame, muduo::Timestamp timestamp)
{
    json request;
    try
    {
        request = json::parse(frame);
    }
    catch (const std::exception &)
    {
        sendProtocolError(connection, "INVALID_JSON", "frame payload is not valid JSON");
        return;
    }
    const auto messageId = request.find("msgid");
    if (messageId == request.end() || !messageId->is_number_integer())
    {
        sendProtocolError(connection, "INVALID_MESSAGE", "integer msgid is required");
        return;
    }
    const int type = messageId->get<int>();
    if (type == HEARTBEAT_MSG)
    {
        dispatchHeartbeat(connection, request);
        return;
    }

    std::weak_ptr<muduo::net::TcpConnection> weakConnection(connection);
    const bool accepted = businessStrand_.trySubmit(
        connection->name(), [this, weakConnection, request = std::move(request), timestamp, type]() mutable {
            const auto liveConnection = weakConnection.lock();
            if (!liveConnection) return;
            auto handler = service_->getHandler(type);
            handler(liveConnection, request, timestamp);
        });
    if (!accepted)
    {
        sendProtocolError(connection, "SERVER_BUSY", "connection business queue is full");
    }
}

void ChatServer::dispatchHeartbeat(const muduo::net::TcpConnectionPtr &connection,
                                   const json &request)
{
    const auto session = service_->sessionFor(connection);
    if (!session || !session->authenticated)
    {
        sendProtocolError(connection, "UNAUTHENTICATED", "heartbeat requires an authenticated session");
        return;
    }

    std::string requestId;
    const auto suppliedId = request.find("request_id");
    if (suppliedId != request.end() && suppliedId->is_string())
    {
        requestId = suppliedId->get<std::string>();
    }
    if (requestId.empty() || requestId.size() > 128)
    {
        requestId = std::to_string(nextRequestId_.fetch_add(1));
    }
    const std::string registryRequestId = session->sessionId + ":" + requestId;
    const auto deadline = nexusim::PendingRequestRegistry::Clock::now() +
                          std::chrono::seconds(5);
    if (!pendingRequests_.add({registryRequestId, session->connectionId,
                               session->sessionId, deadline}))
    {
        sendProtocolError(connection, "DUPLICATE_REQUEST", "request_id is already pending");
        return;
    }

    std::weak_ptr<muduo::net::TcpConnection> weakConnection(connection);
    if (!presenceExecutor_.trySubmit([this, weakConnection, session = *session,
                                      requestId, registryRequestId] {
            const bool renewed = service_->renewPresence(session);
            const auto completion = pendingRequests_.complete(
                registryRequestId, session.connectionId, session.sessionId);
            const auto liveConnection = weakConnection.lock();
            if (!liveConnection || completion != nexusim::PendingRequestRegistry::Completion::Completed ||
                !service_->isCurrentSession(session))
            {
                return;
            }
            json response;
            response["msgid"] = HEARTBEAT_MSG_ACK;
            response["request_id"] = requestId;
            response["ok"] = renewed;
            if (!renewed)
            {
                response["error"] = "PRESENCE_LOST";
                service_->invalidateSession(session);
            }
            nexusim::sendFramed(liveConnection, response.dump());
            if (!renewed) liveConnection->shutdown();
        }))
    {
        pendingRequests_.cancel(registryRequestId);
        sendProtocolError(connection, "SERVER_BUSY", "presence queue is full");
    }
}

void ChatServer::sendProtocolError(const muduo::net::TcpConnectionPtr &connection,
                                   const std::string &code, const std::string &message)
{
    json response;
    response["msgid"] = ERROR_MSG;
    response["error"] = code;
    response["errmsg"] = message;
    nexusim::sendFramed(connection, response.dump());
}
