#ifndef CHATSERVER_H
#define CHATSERVER_H

#include "framecodec.hpp"
#include "json.hpp"
#include "server/concurrency/boundedexecutor.hpp"
#include "server/concurrency/connectionstrand.hpp"
#include "server/concurrency/pendingrequest.hpp"

#include <muduo/net/EventLoop.h>
#include <muduo/net/TcpServer.h>

#include <atomic>
#include <cstdint>
#include <string>

class ChatService;

class ChatServer
{
public:
    ChatServer(muduo::net::EventLoop *loop,
               const muduo::net::InetAddress &listenAddress,
               const std::string &name);
    void start();

private:
    void onConnection(const muduo::net::TcpConnectionPtr &connection);
    void onMessage(const muduo::net::TcpConnectionPtr &connection,
                   muduo::net::Buffer *buffer, muduo::Timestamp timestamp);
    void dispatchFrame(const muduo::net::TcpConnectionPtr &connection,
                       std::string frame, muduo::Timestamp timestamp);
    void dispatchHeartbeat(const muduo::net::TcpConnectionPtr &connection,
                           const nlohmann::json &request);
    void sendProtocolError(const muduo::net::TcpConnectionPtr &connection,
                           const std::string &code, const std::string &message);

    muduo::net::TcpServer server_;
    muduo::net::EventLoop *loop_;
    ChatService *service_;
    nexusim::FrameCodec codec_;
    nexusim::ConnectionStrand businessStrand_;
    nexusim::BoundedExecutor presenceExecutor_;
    nexusim::PendingRequestRegistry pendingRequests_;
    std::atomic<std::uint64_t> nextRequestId_;
};

#endif
