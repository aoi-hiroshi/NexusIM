#ifndef NEXUSIM_WIREPROTOCOL_H
#define NEXUSIM_WIREPROTOCOL_H

#include "framecodec.hpp"

#include <muduo/net/TcpConnection.h>
#include <muduo/net/EventLoop.h>

#include <memory>
#include <string>

namespace nexusim
{

inline void sendFramed(const muduo::net::TcpConnectionPtr &connection,
                       const std::string &payload)
{
    if (!connection)
    {
        return;
    }
    const std::string frame = FrameCodec::encode(payload);
    if (connection->getLoop()->isInLoopThread())
    {
        if (connection->connected()) connection->send(frame);
        return;
    }
    std::weak_ptr<muduo::net::TcpConnection> weakConnection(connection);
    connection->getLoop()->queueInLoop([weakConnection, frame] {
        const auto liveConnection = weakConnection.lock();
        if (liveConnection && liveConnection->connected())
        {
            liveConnection->send(frame);
        }
    });
}

// Use this when a worker must return a final protocol frame and then close.
// Queuing both operations together prevents shutdown() from changing the
// connection state before a separately queued send callback can run.
inline void sendFramedAndShutdown(
    const muduo::net::TcpConnectionPtr &connection,
    const std::string &payload)
{
    if (!connection) return;
    const std::string frame = FrameCodec::encode(payload);
    std::weak_ptr<muduo::net::TcpConnection> weakConnection(connection);
    const auto sendAndShutdown = [weakConnection, frame] {
        const auto liveConnection = weakConnection.lock();
        if (liveConnection && liveConnection->connected())
        {
            liveConnection->send(frame);
            liveConnection->shutdown();
        }
    };
    if (connection->getLoop()->isInLoopThread())
    {
        sendAndShutdown();
    }
    else
    {
        connection->getLoop()->queueInLoop(sendAndShutdown);
    }
}

} // namespace nexusim

#endif
