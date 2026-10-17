#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include "json.hpp"
#include "server/model/friendmodel.hpp"
#include "server/model/offlinemessagemodel.hpp"
#include "server/model/usermodel.hpp"
#include "server/redis/redis.hpp"
#include "server/sessioncontext.hpp"

#include <muduo/base/Timestamp.h>
#include <muduo/net/TcpConnection.h>

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

using json = nlohmann::json;
using MsgHandler = std::function<void(const muduo::net::TcpConnectionPtr &,
                                      json &, muduo::Timestamp)>;

class ChatService
{
public:
    static ChatService *instance();

    MsgHandler getHandler(int msgid);
    void connectionOpened(const muduo::net::TcpConnectionPtr &connection);
    std::optional<nexusim::SessionContext> detachConnection(
        const muduo::net::TcpConnectionPtr &connection);
    void cleanupSession(const nexusim::SessionContext &session);
    std::optional<nexusim::SessionContext> sessionFor(
        const muduo::net::TcpConnectionPtr &connection) const;
    bool isCurrentSession(const nexusim::SessionContext &session) const;
    bool invalidateSession(const nexusim::SessionContext &session);
    bool renewPresence(const nexusim::SessionContext &session);

    void reset();
    void handleRedisSubscribeMessage(int userId, std::string message);

private:
    ChatService();

    void login(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    void reg(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    void oneChat(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    void loginout(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    bool bindAuthenticated(const muduo::net::TcpConnectionPtr &, int userId,
                           const std::string &userName, const std::string &sessionId);
    void sendError(const muduo::net::TcpConnectionPtr &, int requestMsgId,
                   const std::string &code, const std::string &message) const;

    std::unordered_map<int, MsgHandler> handlers_;
    std::unordered_map<int, muduo::net::TcpConnectionPtr> userConnections_;
    std::unordered_map<std::string, nexusim::SessionContext> sessions_;
    mutable std::mutex connectionMutex_;

    UserModel userModel_;
    OfflineMsgModel offlineMessageModel_;
    FriendModel friendModel_;
    Redis redis_;
    std::string nodeId_;
    int presenceTtlSeconds_;
};

#endif
