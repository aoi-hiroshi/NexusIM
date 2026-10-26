#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include "json.hpp"
#include "server/concurrency/boundedexecutor.hpp"
#include "server/concurrency/pendingrequest.hpp"
#include "server/model/friendmodel.hpp"
#include "server/model/groupmodel.hpp"
#include "server/model/offlinemessagemodel.hpp"
#include "server/redis/redis.hpp"
#include "server/rpc/aiserviceclient.hpp"
#include "server/rpc/userserviceclient.hpp"
#include "server/sessioncontext.hpp"

#include <muduo/base/Timestamp.h>
#include <muduo/net/TcpConnection.h>

#include <functional>
#include <atomic>
#include <chrono>
#include <cstdint>
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
    void cancelPendingRequests(const nexusim::SessionContext &session);
    std::optional<nexusim::SessionContext> sessionFor(
        const muduo::net::TcpConnectionPtr &connection) const;
    bool isCurrentSession(const nexusim::SessionContext &session) const;
    bool invalidateSession(const nexusim::SessionContext &session);
    bool renewPresence(const nexusim::SessionContext &session);
    const std::string &nodeId() const noexcept;
    void aiChat(const muduo::net::TcpConnectionPtr &, json &,
                muduo::Timestamp);
    void sendAiError(const muduo::net::TcpConnectionPtr &,
                     const json &request, const std::string &code,
                     const std::string &message) const;

private:
    ChatService();

    void login(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    void reg(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    void oneChat(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    void addFriend(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    void queryFriends(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    void createGroup(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    void addGroup(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    void queryGroups(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    void queryGroupMembers(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    void leaveGroup(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    void groupChat(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    void loginout(const muduo::net::TcpConnectionPtr &, json &, muduo::Timestamp);
    bool bindAuthenticated(const muduo::net::TcpConnectionPtr &, int userId,
                           const std::string &userName, const std::string &sessionId);
    bool bindAuthenticatedOnLoop(const muduo::net::TcpConnectionPtr &, int userId,
                                 const std::string &userName,
                                 const std::string &sessionId,
                                 nexusim::PendingRequestRegistry::Clock::time_point deadline);
    bool isCurrentLifecycle(const nexusim::SessionContext &session) const;
    std::string beginRpcRequest(const nexusim::SessionContext &session,
                                nexusim::PendingRequestRegistry::Clock::time_point deadline);
    bool completeRpcRequest(const std::string &requestId,
                            const nexusim::SessionContext &session,
                            bool *expired = nullptr);
    void scheduleRpcTimeout(
        const muduo::net::TcpConnectionPtr &connection,
        const nexusim::SessionContext &session,
        const std::string &requestId,
        nexusim::PendingRequestRegistry::Clock::time_point deadline,
        std::function<void(const muduo::net::TcpConnectionPtr &)> onTimeout);
    std::chrono::milliseconds rpcTimeout() const;
    std::chrono::milliseconds aiRpcTimeout() const;
    std::string beginAiRpcRequest(
        const nexusim::SessionContext &session,
        const std::string &clientRequestId,
        nexusim::PendingRequestRegistry::Clock::time_point deadline);
    muduo::net::TcpConnectionPtr localConnectionForUser(
        int userId, const std::string *expectedSessionId = nullptr) const;
    void enqueueRedisDelivery(nexusim::ClusterDelivery delivery);
    void handleRedisDelivery(const nexusim::ClusterDelivery &delivery);
    void storeOfflineOrReport(const muduo::net::TcpConnectionPtr &sender,
                              int recipientId, const std::string &payload,
                              const std::string &errorCode,
                              const std::string &errorMessage);
    void sendError(const muduo::net::TcpConnectionPtr &, int requestMsgId,
                   const std::string &code, const std::string &message,
                   std::optional<int> recipientId = std::nullopt,
                   std::optional<bool> queuedOffline = std::nullopt) const;

    std::unordered_map<int, MsgHandler> handlers_;
    std::unordered_map<int, muduo::net::TcpConnectionPtr> userConnections_;
    std::unordered_map<std::string, nexusim::SessionContext> sessions_;
    mutable std::mutex connectionMutex_;

    OfflineMsgModel offlineMessageModel_;
    FriendModel friendModel_;
    GroupModel groupModel_;
    nexusim::UserServiceClient userServiceClient_;
    nexusim::AIServiceClient aiServiceClient_;
    nexusim::PendingRequestRegistry rpcRequests_;
    std::atomic<std::uint64_t> nextRpcRequestId_{1};
    std::string nodeId_;
    int presenceTtlSeconds_;
    nexusim::BoundedExecutor redisInboundExecutor_;
    Redis redis_;
};

#endif
