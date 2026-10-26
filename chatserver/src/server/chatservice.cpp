#include "server/chatservice.hpp"

#include "public.hpp"
#include "server/security/passwordhasher.hpp"
#include "server/wireprotocol.hpp"

#include <muduo/base/Logging.h>

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <condition_variable>
#include <memory>
#include <utility>
#include <vector>

namespace
{
std::string environmentOr(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    return value == nullptr || *value == '\0' ? std::string(fallback) : std::string(value);
}

int environmentInt(const char *name, int fallback)
{
    try
    {
        const std::string fallbackText = std::to_string(fallback);
        return std::stoi(environmentOr(name, fallbackText.c_str()));
    }
    catch (...)
    {
        return fallback;
    }
}

bool stringField(const json &value, const char *name, std::string &result)
{
    const auto it = value.find(name);
    if (it == value.end() || !it->is_string()) return false;
    result = it->get<std::string>();
    return true;
}

bool intField(const json &value, const char *name, int &result)
{
    const auto it = value.find(name);
    if (it == value.end() || !it->is_number_integer()) return false;
    result = it->get<int>();
    return true;
}

const char *rpcErrorName(mprpc::RpcErrorCode code)
{
    switch (code)
    {
    case mprpc::RpcErrorCode::DeadlineExceeded:
        return "RPC_TIMEOUT";
    case mprpc::RpcErrorCode::ServerBusy:
        return "RPC_SERVER_BUSY";
    case mprpc::RpcErrorCode::DiscoveryUnavailable:
        return "ZOOKEEPER_UNAVAILABLE";
    case mprpc::RpcErrorCode::ServiceNotFound:
        return "USER_SERVICE_UNAVAILABLE";
    default:
        return "RPC_UNAVAILABLE";
    }
}

const char *aiRpcErrorName(mprpc::RpcErrorCode code)
{
    switch (code)
    {
    case mprpc::RpcErrorCode::DeadlineExceeded:
        return "AI_UPSTREAM_TIMEOUT";
    case mprpc::RpcErrorCode::ServerBusy:
        return "AI_SERVICE_BUSY";
    case mprpc::RpcErrorCode::Cancelled:
        return "AI_REQUEST_CANCELLED";
    case mprpc::RpcErrorCode::DiscoveryUnavailable:
    case mprpc::RpcErrorCode::ServiceNotFound:
        return "AI_SERVICE_UNAVAILABLE";
    default:
        return "AI_SERVICE_UNAVAILABLE";
    }
}

bool validOpaqueId(const std::string &value)
{
    if (value.empty() || value.size() > 128) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '-' ||
               character == '_' || character == '.' || character == ':';
    });
}

bool requestIdField(const json &request, std::string &requestId)
{
    return stringField(request, "request_id", requestId) && validOpaqueId(requestId);
}

void sendBusinessResponse(const muduo::net::TcpConnectionPtr &connection,
                          int responseType, const std::string &requestId,
                          bool ok, const std::string &error = {},
                          const std::string &message = {},
                          json fields = json::object())
{
    fields["msgid"] = responseType;
    fields["request_id"] = requestId;
    fields["ok"] = ok;
    if (!error.empty()) fields["error"] = error;
    if (!message.empty()) fields["errmsg"] = message;
    nexusim::sendFramed(connection, fields.dump());
}

std::string serverTimeText()
{
    std::string value = muduo::Timestamp::now().toFormattedString(false);
    if (value.size() >= 8)
    {
        value.insert(4, "-");
        value.insert(7, "-");
    }
    return value;
}
} // namespace

ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

ChatService::ChatService()
    : nodeId_(environmentOr("CHAT_NODE_ID", "chatserver-local")),
      presenceTtlSeconds_(environmentInt("CHAT_PRESENCE_TTL_SECONDS", 90)),
      redisInboundExecutor_("redis-inbound", 1, 512)
{
    // Explicit allowlist: only handlers that have passed the Stage 1-6 security
    // review are reachable over the network.
    handlers_.emplace(LOGIN_MSG, std::bind(&ChatService::login, this,
                                           std::placeholders::_1,
                                           std::placeholders::_2,
                                           std::placeholders::_3));
    handlers_.emplace(REG_MSG, std::bind(&ChatService::reg, this,
                                         std::placeholders::_1,
                                         std::placeholders::_2,
                                         std::placeholders::_3));
    handlers_.emplace(ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2,
                                              std::placeholders::_3));
    handlers_.emplace(LOGINOUT_MSG, std::bind(&ChatService::loginout, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2,
                                              std::placeholders::_3));
    handlers_.emplace(ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this,
                                                std::placeholders::_1,
                                                std::placeholders::_2,
                                                std::placeholders::_3));
    handlers_.emplace(QUERY_FRIENDS_MSG, std::bind(&ChatService::queryFriends, this,
                                                   std::placeholders::_1,
                                                   std::placeholders::_2,
                                                   std::placeholders::_3));
    handlers_.emplace(CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this,
                                                  std::placeholders::_1,
                                                  std::placeholders::_2,
                                                  std::placeholders::_3));
    handlers_.emplace(ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this,
                                               std::placeholders::_1,
                                               std::placeholders::_2,
                                               std::placeholders::_3));
    handlers_.emplace(QUERY_GROUPS_MSG, std::bind(&ChatService::queryGroups, this,
                                                  std::placeholders::_1,
                                                  std::placeholders::_2,
                                                  std::placeholders::_3));
    handlers_.emplace(QUERY_GROUP_MEMBERS_MSG,
                      std::bind(&ChatService::queryGroupMembers, this,
                                std::placeholders::_1, std::placeholders::_2,
                                std::placeholders::_3));
    handlers_.emplace(LEAVE_GROUP_MSG, std::bind(&ChatService::leaveGroup, this,
                                                 std::placeholders::_1,
                                                 std::placeholders::_2,
                                                 std::placeholders::_3));
    handlers_.emplace(GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this,
                                                std::placeholders::_1,
                                                std::placeholders::_2,
                                                std::placeholders::_3));

    redis_.initNotifyHandler([this](nexusim::ClusterDelivery delivery) {
        enqueueRedisDelivery(std::move(delivery));
    });
    if (!redis_.connect(nodeId_))
    {
        LOG_ERROR << "node=" << nodeId_
                  << " Redis unavailable; login fails closed because Presence cannot be acquired";
    }
    LOG_INFO << "NexusIM ChatService initialized node=" << nodeId_
             << " presence_ttl=" << presenceTtlSeconds_;
}

MsgHandler ChatService::getHandler(int msgid)
{
    const auto iterator = handlers_.find(msgid);
    if (iterator != handlers_.end()) return iterator->second;
    return [this, msgid](const muduo::net::TcpConnectionPtr &connection,
                         json &, muduo::Timestamp) {
        sendError(connection, msgid, "FEATURE_DISABLED",
                  "message type is not enabled in NexusIM V1");
    };
}

void ChatService::connectionOpened(const muduo::net::TcpConnectionPtr &connection)
{
    nexusim::SessionContext context;
    context.connectionId = connection->name();
    context.lifecycleId = nexusim::generateSessionId();
    std::lock_guard<std::mutex> lock(connectionMutex_);
    sessions_[context.connectionId] = std::move(context);
}

std::optional<nexusim::SessionContext> ChatService::detachConnection(
    const muduo::net::TcpConnectionPtr &connection)
{
    std::lock_guard<std::mutex> lock(connectionMutex_);
    const auto iterator = sessions_.find(connection->name());
    if (iterator == sessions_.end()) return std::nullopt;
    const nexusim::SessionContext result = iterator->second;
    if (result.authenticated)
    {
        const auto user = userConnections_.find(result.userId);
        if (user != userConnections_.end() && user->second == connection)
        {
            userConnections_.erase(user);
        }
    }
    sessions_.erase(iterator);
    return result;
}

void ChatService::cleanupSession(const nexusim::SessionContext &session)
{
    if (!session.authenticated) return;
    if (!redis_.releasePresence(session.userId, nodeId_, session.connectionId,
                                session.sessionId))
    {
        LOG_WARN << "node=" << nodeId_ << " did not release Presence for user="
                 << session.userId << "; key is stale, unavailable, or owned by a newer session";
    }
}

void ChatService::cancelPendingRequests(const nexusim::SessionContext &session)
{
    rpcRequests_.cancelConnection(session.connectionId, session.lifecycleId);
}

std::optional<nexusim::SessionContext> ChatService::sessionFor(
    const muduo::net::TcpConnectionPtr &connection) const
{
    std::lock_guard<std::mutex> lock(connectionMutex_);
    const auto iterator = sessions_.find(connection->name());
    if (iterator == sessions_.end()) return std::nullopt;
    return iterator->second;
}

bool ChatService::isCurrentSession(const nexusim::SessionContext &session) const
{
    std::lock_guard<std::mutex> lock(connectionMutex_);
    const auto iterator = sessions_.find(session.connectionId);
    return iterator != sessions_.end() && iterator->second.authenticated &&
           iterator->second.userId == session.userId &&
           iterator->second.sessionId == session.sessionId;
}

bool ChatService::invalidateSession(const nexusim::SessionContext &session)
{
    std::lock_guard<std::mutex> lock(connectionMutex_);
    const auto iterator = sessions_.find(session.connectionId);
    if (iterator == sessions_.end() || !iterator->second.authenticated ||
        iterator->second.userId != session.userId ||
        iterator->second.sessionId != session.sessionId)
    {
        return false;
    }
    const auto user = userConnections_.find(session.userId);
    if (user != userConnections_.end()) userConnections_.erase(user);
    sessions_.erase(iterator);
    return true;
}

bool ChatService::renewPresence(const nexusim::SessionContext &session)
{
    return session.authenticated && isCurrentSession(session) &&
           redis_.renewPresence(session.userId, nodeId_, session.connectionId,
                                session.sessionId, presenceTtlSeconds_);
}

const std::string &ChatService::nodeId() const noexcept
{
    return nodeId_;
}

std::chrono::milliseconds ChatService::rpcTimeout() const
{
    const int configured = environmentInt("CHAT_RPC_DEADLINE_MS", 2000);
    return std::chrono::milliseconds(
        configured < 100 || configured > 30000 ? 2000 : configured);
}

std::chrono::milliseconds ChatService::aiRpcTimeout() const
{
    const int configured = environmentInt("CHAT_AI_RPC_DEADLINE_MS", 85000);
    return std::chrono::milliseconds(
        configured < 1000 || configured > 120000 ? 85000 : configured);
}

bool ChatService::isCurrentLifecycle(
    const nexusim::SessionContext &session) const
{
    std::lock_guard<std::mutex> lock(connectionMutex_);
    const auto current = sessions_.find(session.connectionId);
    return current != sessions_.end() &&
           current->second.lifecycleId == session.lifecycleId;
}

std::string ChatService::beginRpcRequest(
    const nexusim::SessionContext &session,
    nexusim::PendingRequestRegistry::Clock::time_point deadline)
{
    const std::string requestId = nodeId_ + "-rpc-" +
                                  std::to_string(nextRpcRequestId_.fetch_add(1));
    if (!rpcRequests_.add(
            {requestId, session.connectionId, session.lifecycleId, deadline}))
        return "";
    return requestId;
}

std::string ChatService::beginAiRpcRequest(
    const nexusim::SessionContext &session,
    const std::string &clientRequestId,
    nexusim::PendingRequestRegistry::Clock::time_point deadline)
{
    const std::string requestId = nodeId_ + "-ai-" + session.sessionId + "-" +
                                  clientRequestId;
    if (!rpcRequests_.add(
            {requestId, session.connectionId, session.lifecycleId, deadline}))
        return "";
    return requestId;
}

bool ChatService::completeRpcRequest(
    const std::string &requestId, const nexusim::SessionContext &session,
    bool *expired)
{
    const auto completion = rpcRequests_.complete(
        requestId, session.connectionId, session.lifecycleId);
    if (expired != nullptr)
        *expired = completion == nexusim::PendingRequestRegistry::Completion::Expired;
    return (completion == nexusim::PendingRequestRegistry::Completion::Completed ||
            completion == nexusim::PendingRequestRegistry::Completion::Expired) &&
           isCurrentLifecycle(session);
}

void ChatService::scheduleRpcTimeout(
    const muduo::net::TcpConnectionPtr &connection,
    const nexusim::SessionContext &session, const std::string &requestId,
    nexusim::PendingRequestRegistry::Clock::time_point deadline,
    std::function<void(const muduo::net::TcpConnectionPtr &)> onTimeout)
{
    const auto remaining = deadline - nexusim::PendingRequestRegistry::Clock::now();
    const double seconds = std::max(
        0.0, std::chrono::duration<double>(remaining).count()) + 0.01;
    std::weak_ptr<muduo::net::TcpConnection> weakConnection(connection);
    connection->getLoop()->runAfter(
        seconds, [this, weakConnection, session, requestId,
                  onTimeout = std::move(onTimeout)] {
            const auto liveConnection = weakConnection.lock();
            if (!liveConnection || !liveConnection->connected()) return;
            if (!completeRpcRequest(requestId, session) ||
                (session.authenticated && !isCurrentSession(session))) return;
            onTimeout(liveConnection);
        });
}

bool ChatService::bindAuthenticatedOnLoop(
    const muduo::net::TcpConnectionPtr &connection, int userId,
    const std::string &userName, const std::string &sessionId,
    nexusim::PendingRequestRegistry::Clock::time_point deadline)
{
    struct CommitState
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool cancelled{false};
        bool finished{false};
        bool result{false};
    };

    auto state = std::make_shared<CommitState>();
    std::weak_ptr<muduo::net::TcpConnection> weakConnection(connection);
    connection->getLoop()->queueInLoop(
        [this, state, weakConnection, userId, userName, sessionId] {
            std::unique_lock<std::mutex> lock(state->mutex);
            if (state->cancelled) return;
            const auto liveConnection = weakConnection.lock();
            state->result = liveConnection && liveConnection->connected() &&
                            bindAuthenticated(liveConnection, userId, userName,
                                              sessionId);
            state->finished = true;
            lock.unlock();
            state->changed.notify_one();
        });

    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->changed.wait_until(lock, deadline,
                                   [&state] { return state->finished; }))
    {
        state->cancelled = true;
        return false;
    }
    return state->result;
}

bool ChatService::bindAuthenticated(const muduo::net::TcpConnectionPtr &connection,
                                    int userId, const std::string &userName,
                                    const std::string &sessionId)
{
    std::lock_guard<std::mutex> lock(connectionMutex_);
    const auto session = sessions_.find(connection->name());
    if (session == sessions_.end() || session->second.authenticated ||
        userConnections_.find(userId) != userConnections_.end())
    {
        return false;
    }
    session->second.authenticated = true;
    session->second.userId = userId;
    session->second.userName = userName;
    session->second.sessionId = sessionId;
    userConnections_[userId] = connection;
    return true;
}

void ChatService::login(const muduo::net::TcpConnectionPtr &connection,
                        json &request, muduo::Timestamp)
{
    int id = -1;
    std::string password;
    if (!intField(request, "id", id) || !stringField(request, "password", password) ||
        id <= 0 || password.empty() || password.size() > 256)
    {
        sendError(connection, LOGIN_MSG, "INVALID_ARGUMENT", "invalid id or password");
        return;
    }
    const auto existing = sessionFor(connection);
    if (!existing || existing->authenticated)
    {
        sendError(connection, LOGIN_MSG, "INVALID_STATE", "connection is not eligible to login");
        return;
    }

    json response;
    response["msgid"] = LOGIN_MSG_ACK;
    const auto deadline = nexusim::PendingRequestRegistry::Clock::now() + rpcTimeout();
    const std::string rpcRequestId = beginRpcRequest(*existing, deadline);
    if (rpcRequestId.empty())
    {
        response["errno"] = 6;
        response["error"] = "SERVER_BUSY";
        response["errmsg"] = "could not track login RPC";
        nexusim::sendFramed(connection, response.dump());
        return;
    }
    scheduleRpcTimeout(connection, *existing, rpcRequestId, deadline,
        [](const muduo::net::TcpConnectionPtr &liveConnection) {
            json timeoutResponse{{"msgid", LOGIN_MSG_ACK}, {"errno", 6},
                                 {"error", "RPC_TIMEOUT"},
                                 {"errmsg", "login RPC deadline exceeded"}};
            nexusim::sendFramed(liveConnection, timeoutResponse.dump());
        });

    const auto rpcResult = userServiceClient_.login(id, password, deadline);
    bool expired = false;
    if (!completeRpcRequest(rpcRequestId, *existing, &expired))
    {
        // The connection was closed/replaced or the loop already sent a timeout.
        return;
    }
    if (expired)
    {
        response["errno"] = 6;
        response["error"] = "RPC_TIMEOUT";
        response["errmsg"] = "login RPC deadline exceeded";
        nexusim::sendFramed(connection, response.dump());
        return;
    }
    if (!rpcResult.call.ok)
    {
        response["errno"] = 6;
        response["error"] = rpcErrorName(rpcResult.call.error);
        response["errmsg"] = rpcResult.call.message;
        nexusim::sendFramed(connection, response.dump());
        return;
    }
    if (!rpcResult.authenticated || rpcResult.userId != id)
    {
        response["errno"] = 1;
        response["error"] = "INVALID_CREDENTIALS";
        response["errmsg"] = rpcResult.userMessage.empty()
                                  ? "id or password is invalid"
                                  : rpcResult.userMessage;
        nexusim::sendFramed(connection, response.dump());
        return;
    }

    if (!redis_.subscriptionHealthy())
    {
        response["errno"] = 4;
        response["errmsg"] = "cluster subscription is unavailable";
        nexusim::sendFramed(connection, response.dump());
        return;
    }

    const std::string sessionId = nexusim::generateSessionId();
    const auto acquire = redis_.acquirePresence(
        id, nodeId_, connection->name(), sessionId, presenceTtlSeconds_);
    if (acquire != Redis::AcquireStatus::Acquired)
    {
        response["errno"] = acquire == Redis::AcquireStatus::AlreadyOwned ? 2 : 4;
        response["errmsg"] = acquire == Redis::AcquireStatus::AlreadyOwned
                                  ? "account is already online"
                                  : "presence service is unavailable";
        nexusim::sendFramed(connection, response.dump());
        return;
    }
    if (!bindAuthenticatedOnLoop(connection, id, rpcResult.username, sessionId,
                                 deadline))
    {
        redis_.releasePresence(id, nodeId_, connection->name(), sessionId);
        response["errno"] = 3;
        response["errmsg"] = "connection closed or login state changed";
        if (connection->connected()) nexusim::sendFramed(connection, response.dump());
        return;
    }

    response["errno"] = 0;
    response["id"] = rpcResult.userId;
    response["name"] = rpcResult.username;
    response["session_id"] = sessionId;
    response["node_id"] = nodeId_;

    std::vector<std::string> offline = offlineMessageModel_.query(id);
    if (!offline.empty())
    {
        response["offlinemsg"] = offline;
        offlineMessageModel_.remove(id);
    }
    std::vector<User> friends = friendModel_.query(id);
    if (!friends.empty())
    {
        std::vector<std::string> encodedFriends;
        for (User &friendUser : friends)
        {
            json value;
            value["id"] = friendUser.getId();
            value["name"] = friendUser.getName();
            value["state"] = friendUser.getState();
            encodedFriends.push_back(value.dump());
        }
        response["friends"] = encodedFriends;
    }
    bool groupsLoaded = false;
    const auto groups = groupModel_.queryGroups(id, &groupsLoaded);
    if (groupsLoaded && !groups.empty())
    {
        std::vector<std::string> encodedGroups;
        for (const Group &group : groups)
        {
            json value{{"id", group.getId()}, {"groupname", group.getName()},
                       {"groupdesc", group.getDesc()}, {"role", group.getRole()}};
            bool authorized = false;
            bool membersLoaded = false;
            const auto members = groupModel_.queryMembers(
                id, group.getId(), authorized, membersLoaded);
            if (authorized && membersLoaded)
            {
                std::vector<std::string> encodedMembers;
                for (const GroupUser &member : members)
                {
                    json memberValue{{"id", member.getId()},
                                     {"name", member.getName()},
                                     {"state", "unknown"},
                                     {"role", member.getRole()}};
                    encodedMembers.push_back(memberValue.dump());
                }
                value["users"] = std::move(encodedMembers);
            }
            encodedGroups.push_back(value.dump());
        }
        response["groups"] = std::move(encodedGroups);
    }
    nexusim::sendFramed(connection, response.dump());
    LOG_INFO << "node=" << nodeId_ << " user=" << id
             << " login connection=" << connection->name();
}

void ChatService::reg(const muduo::net::TcpConnectionPtr &connection,
                      json &request, muduo::Timestamp)
{
    std::string name;
    std::string password;
    if (!stringField(request, "name", name) || !stringField(request, "password", password) ||
        name.empty() || name.size() > 50 || password.size() < 8 || password.size() > 128)
    {
        sendError(connection, REG_MSG, "INVALID_ARGUMENT",
                  "name must contain 1-50 bytes and password must contain 8-128 bytes");
        return;
    }
    const auto session = sessionFor(connection);
    if (!session || session->authenticated)
    {
        sendError(connection, REG_MSG, "INVALID_STATE", "register requires an unauthenticated connection");
        return;
    }

    json response;
    response["msgid"] = REG_MSG_ACK;
    const auto deadline = nexusim::PendingRequestRegistry::Clock::now() + rpcTimeout();
    const std::string rpcRequestId = beginRpcRequest(*session, deadline);
    if (rpcRequestId.empty())
    {
        response["errno"] = 2;
        response["error"] = "SERVER_BUSY";
        response["errmsg"] = "could not track register RPC";
        nexusim::sendFramed(connection, response.dump());
        return;
    }
    scheduleRpcTimeout(connection, *session, rpcRequestId, deadline,
        [](const muduo::net::TcpConnectionPtr &liveConnection) {
            json timeoutResponse{{"msgid", REG_MSG_ACK}, {"errno", 2},
                                 {"error", "RPC_TIMEOUT"},
                                 {"errmsg", "register RPC deadline exceeded"}};
            nexusim::sendFramed(liveConnection, timeoutResponse.dump());
        });

    const auto rpcResult = userServiceClient_.registerUser(name, password, deadline);
    bool expired = false;
    if (!completeRpcRequest(rpcRequestId, *session, &expired)) return;
    if (expired)
    {
        response["errno"] = 2;
        response["error"] = "RPC_TIMEOUT";
        response["errmsg"] = "register RPC deadline exceeded";
        nexusim::sendFramed(connection, response.dump());
        return;
    }
    if (!rpcResult.call.ok)
    {
        response["errno"] = 2;
        response["error"] = rpcErrorName(rpcResult.call.error);
        response["errmsg"] = rpcResult.call.message;
    }
    else if (rpcResult.registered)
    {
        response["errno"] = 0;
        response["id"] = rpcResult.userId;
    }
    else
    {
        response["errno"] = 1;
        response["error"] = "REGISTER_FAILED";
        response["errmsg"] = rpcResult.userMessage.empty()
                                  ? "registration failed"
                                  : rpcResult.userMessage;
    }
    nexusim::sendFramed(connection, response.dump());
}

void ChatService::oneChat(const muduo::net::TcpConnectionPtr &connection,
                          json &request, muduo::Timestamp)
{
    const auto session = sessionFor(connection);
    int recipientId = -1;
    std::string message;
    if (!session || !session->authenticated)
    {
        sendError(connection, ONE_CHAT_MSG, "UNAUTHENTICATED", "login is required");
        return;
    }
    if (!intField(request, "toid", recipientId) || recipientId <= 0 ||
        !stringField(request, "msg", message) || message.empty() || message.size() > 65536)
    {
        sendError(connection, ONE_CHAT_MSG, "INVALID_ARGUMENT", "invalid recipient or message");
        return;
    }

    // The authenticated context is authoritative; client-supplied id/name are overwritten.
    request["id"] = session->userId;
    request["name"] = session->userName;
    request.erase("session_id");
    const std::string payload = request.dump();

    const auto localRecipient = localConnectionForUser(recipientId);
    if (localRecipient)
    {
        nexusim::sendFramed(localRecipient, payload);
        LOG_INFO << "node=" << nodeId_ << " direct-chat local sender="
                 << session->userId << " recipient=" << recipientId;
        return;
    }

    const auto lookup = redis_.lookupPresence(recipientId);
    if (lookup.status == Redis::LookupStatus::NotFound)
    {
        storeOfflineOrReport(connection, recipientId, payload, "OFFLINE_STORE_FAILED",
                             "recipient is offline and the message could not be stored");
        LOG_INFO << "node=" << nodeId_ << " direct-chat offline sender="
                 << session->userId << " recipient=" << recipientId;
        return;
    }
    if (lookup.status == Redis::LookupStatus::Error)
    {
        storeOfflineOrReport(connection, recipientId, payload, "PRESENCE_UNAVAILABLE",
                             "presence lookup failed; message was not delivered in real time");
        return;
    }

    const nexusim::ClusterDelivery delivery{recipientId,
                                             lookup.presence.sessionId,
                                             payload};
    const auto published = redis_.publishToNode(lookup.presence.nodeId, delivery);
    if (published.status != Redis::PublishStatus::Published)
    {
        storeOfflineOrReport(connection, recipientId, payload, "REDIS_DELIVERY_FAILED",
                             "cross-node publish failed; message was not delivered in real time");
        return;
    }
    if (published.subscriberCount != 1)
    {
        LOG_WARN << "node=" << nodeId_ << " target_node=" << lookup.presence.nodeId
                 << " subscriber_count=" << published.subscriberCount
                 << " (node_id values must be unique)";
    }
    LOG_INFO << "node=" << nodeId_ << " direct-chat redis sender="
             << session->userId << " recipient=" << recipientId
             << " target_node=" << lookup.presence.nodeId;
}

void ChatService::addFriend(const muduo::net::TcpConnectionPtr &connection,
                            json &request, muduo::Timestamp)
{
    const auto session = sessionFor(connection);
    std::string requestId;
    int friendId = -1;
    if (!session || !session->authenticated)
    {
        sendBusinessResponse(connection, ADD_FRIEND_MSG_ACK, requestId, false,
                             "UNAUTHENTICATED", "login is required");
        return;
    }
    if (!requestIdField(request, requestId) ||
        !intField(request, "friendid", friendId) || friendId <= 0)
    {
        sendBusinessResponse(connection, ADD_FRIEND_MSG_ACK, requestId, false,
                             "INVALID_ARGUMENT", "valid request_id and friendid are required");
        return;
    }
    if (friendId == session->userId)
    {
        sendBusinessResponse(connection, ADD_FRIEND_MSG_ACK, requestId, false,
                             "CANNOT_ADD_SELF", "a user cannot add itself");
        return;
    }

    const auto deadline = nexusim::PendingRequestRegistry::Clock::now() + rpcTimeout();
    const std::string tracked = beginRpcRequest(*session, deadline);
    if (tracked.empty())
    {
        sendBusinessResponse(connection, ADD_FRIEND_MSG_ACK, requestId, false,
                             "SERVER_BUSY", "could not track user query");
        return;
    }
    scheduleRpcTimeout(connection, *session, tracked, deadline,
        [this, requestId](const muduo::net::TcpConnectionPtr &liveConnection) {
            sendBusinessResponse(liveConnection, ADD_FRIEND_MSG_ACK, requestId,
                                 false, "RPC_TIMEOUT", "user query deadline exceeded");
        });
    const auto user = userServiceClient_.queryUser(friendId, deadline);
    bool expired = false;
    if (!completeRpcRequest(tracked, *session, &expired) ||
        !isCurrentSession(*session)) return;
    if (expired)
    {
        sendBusinessResponse(connection, ADD_FRIEND_MSG_ACK, requestId, false,
                             "RPC_TIMEOUT", "user query deadline exceeded");
        return;
    }
    if (!user.call.ok)
    {
        sendBusinessResponse(connection, ADD_FRIEND_MSG_ACK, requestId, false,
                             rpcErrorName(user.call.error), user.call.message);
        return;
    }
    if (!user.found)
    {
        sendBusinessResponse(connection, ADD_FRIEND_MSG_ACK, requestId, false,
                             "USER_NOT_FOUND", "target user does not exist");
        return;
    }

    const AddFriendStatus status = friendModel_.addBidirectional(
        session->userId, friendId);
    if (status == AddFriendStatus::Error)
    {
        sendBusinessResponse(connection, ADD_FRIEND_MSG_ACK, requestId, false,
                             "DATABASE_ERROR", "friend relationship could not be saved");
        return;
    }
    if (status == AddFriendStatus::UserNotFound)
    {
        sendBusinessResponse(connection, ADD_FRIEND_MSG_ACK, requestId, false,
                             "USER_NOT_FOUND", "target user does not exist");
        return;
    }
    json fields{{"friend", {{"id", friendId}, {"name", user.username}}},
                {"already_friends", status == AddFriendStatus::AlreadyFriends}};
    sendBusinessResponse(connection, ADD_FRIEND_MSG_ACK, requestId, true, {}, {},
                         std::move(fields));
}

void ChatService::queryFriends(const muduo::net::TcpConnectionPtr &connection,
                               json &request, muduo::Timestamp)
{
    const auto session = sessionFor(connection);
    std::string requestId;
    if (!session || !session->authenticated)
    {
        sendBusinessResponse(connection, QUERY_FRIENDS_MSG_ACK, requestId, false,
                             "UNAUTHENTICATED", "login is required");
        return;
    }
    if (!requestIdField(request, requestId))
    {
        sendBusinessResponse(connection, QUERY_FRIENDS_MSG_ACK, requestId, false,
                             "INVALID_ARGUMENT", "valid request_id is required");
        return;
    }
    bool succeeded = false;
    const auto friends = friendModel_.query(session->userId, &succeeded);
    if (!succeeded)
    {
        sendBusinessResponse(connection, QUERY_FRIENDS_MSG_ACK, requestId, false,
                             "DATABASE_ERROR", "friend list could not be loaded");
        return;
    }
    json values = json::array();
    for (const User &friendUser : friends)
        values.push_back({{"id", friendUser.getId()}, {"name", friendUser.getName()}});
    sendBusinessResponse(connection, QUERY_FRIENDS_MSG_ACK, requestId, true, {}, {},
                         {{"friends", std::move(values)}});
}

void ChatService::createGroup(const muduo::net::TcpConnectionPtr &connection,
                              json &request, muduo::Timestamp)
{
    const auto session = sessionFor(connection);
    std::string requestId;
    std::string name;
    std::string description;
    if (!session || !session->authenticated)
    {
        sendBusinessResponse(connection, CREATE_GROUP_MSG_ACK, requestId, false,
                             "UNAUTHENTICATED", "login is required");
        return;
    }
    if (!requestIdField(request, requestId) || !stringField(request, "groupname", name) ||
        !stringField(request, "groupdesc", description) || name.empty() ||
        name.size() > 50 || description.size() > 200)
    {
        sendBusinessResponse(connection, CREATE_GROUP_MSG_ACK, requestId, false,
                             "INVALID_ARGUMENT", "invalid group name or description");
        return;
    }
    Group group(-1, name, description);
    const auto status = groupModel_.createGroup(group, session->userId);
    if (status != CreateGroupStatus::Created)
    {
        sendBusinessResponse(connection, CREATE_GROUP_MSG_ACK, requestId, false,
                             status == CreateGroupStatus::InvalidUser
                                 ? "USER_NOT_FOUND" : "DATABASE_ERROR",
                             "group could not be created");
        return;
    }
    sendBusinessResponse(connection, CREATE_GROUP_MSG_ACK, requestId, true, {}, {},
                         {{"group", {{"id", group.getId()},
                                     {"name", group.getName()},
                                     {"description", group.getDesc()},
                                     {"role", group.getRole()}}}});
}

void ChatService::addGroup(const muduo::net::TcpConnectionPtr &connection,
                           json &request, muduo::Timestamp)
{
    const auto session = sessionFor(connection);
    std::string requestId;
    int groupId = -1;
    if (!session || !session->authenticated)
    {
        sendBusinessResponse(connection, ADD_GROUP_MSG_ACK, requestId, false,
                             "UNAUTHENTICATED", "login is required");
        return;
    }
    if (!requestIdField(request, requestId) || !intField(request, "groupid", groupId) ||
        groupId <= 0)
    {
        sendBusinessResponse(connection, ADD_GROUP_MSG_ACK, requestId, false,
                             "INVALID_ARGUMENT", "valid request_id and groupid are required");
        return;
    }
    const auto status = groupModel_.joinGroup(session->userId, groupId);
    if (status == JoinGroupStatus::Error || status == JoinGroupStatus::GroupNotFound)
    {
        sendBusinessResponse(connection, ADD_GROUP_MSG_ACK, requestId, false,
                             status == JoinGroupStatus::GroupNotFound
                                 ? "GROUP_NOT_FOUND" : "DATABASE_ERROR",
                             status == JoinGroupStatus::GroupNotFound
                                 ? "group does not exist" : "group membership could not be saved");
        return;
    }
    sendBusinessResponse(connection, ADD_GROUP_MSG_ACK, requestId, true, {}, {},
                         {{"group_id", groupId},
                          {"already_member", status == JoinGroupStatus::AlreadyMember}});
}

void ChatService::queryGroups(const muduo::net::TcpConnectionPtr &connection,
                              json &request, muduo::Timestamp)
{
    const auto session = sessionFor(connection);
    std::string requestId;
    if (!session || !session->authenticated)
    {
        sendBusinessResponse(connection, QUERY_GROUPS_MSG_ACK, requestId, false,
                             "UNAUTHENTICATED", "login is required");
        return;
    }
    if (!requestIdField(request, requestId))
    {
        sendBusinessResponse(connection, QUERY_GROUPS_MSG_ACK, requestId, false,
                             "INVALID_ARGUMENT", "valid request_id is required");
        return;
    }
    bool succeeded = false;
    const auto groups = groupModel_.queryGroups(session->userId, &succeeded);
    if (!succeeded)
    {
        sendBusinessResponse(connection, QUERY_GROUPS_MSG_ACK, requestId, false,
                             "DATABASE_ERROR", "group list could not be loaded");
        return;
    }
    json values = json::array();
    for (const Group &group : groups)
        values.push_back({{"id", group.getId()}, {"name", group.getName()},
                          {"description", group.getDesc()}, {"role", group.getRole()}});
    sendBusinessResponse(connection, QUERY_GROUPS_MSG_ACK, requestId, true, {}, {},
                         {{"groups", std::move(values)}});
}

void ChatService::queryGroupMembers(const muduo::net::TcpConnectionPtr &connection,
                                    json &request, muduo::Timestamp)
{
    const auto session = sessionFor(connection);
    std::string requestId;
    int groupId = -1;
    if (!session || !session->authenticated)
    {
        sendBusinessResponse(connection, QUERY_GROUP_MEMBERS_MSG_ACK, requestId,
                             false, "UNAUTHENTICATED", "login is required");
        return;
    }
    if (!requestIdField(request, requestId) || !intField(request, "groupid", groupId) ||
        groupId <= 0)
    {
        sendBusinessResponse(connection, QUERY_GROUP_MEMBERS_MSG_ACK, requestId,
                             false, "INVALID_ARGUMENT",
                             "valid request_id and groupid are required");
        return;
    }
    bool authorized = false;
    bool succeeded = false;
    const auto members = groupModel_.queryMembers(
        session->userId, groupId, authorized, succeeded);
    if (!succeeded || !authorized)
    {
        sendBusinessResponse(connection, QUERY_GROUP_MEMBERS_MSG_ACK, requestId,
                             false, succeeded ? "NOT_GROUP_MEMBER" : "DATABASE_ERROR",
                             succeeded ? "group membership is required"
                                       : "group members could not be loaded");
        return;
    }
    json values = json::array();
    for (const GroupUser &member : members)
        values.push_back({{"id", member.getId()}, {"name", member.getName()},
                          {"role", member.getRole()}});
    sendBusinessResponse(connection, QUERY_GROUP_MEMBERS_MSG_ACK, requestId, true,
                         {}, {}, {{"group_id", groupId},
                                  {"members", std::move(values)}});
}

void ChatService::leaveGroup(const muduo::net::TcpConnectionPtr &connection,
                             json &request, muduo::Timestamp)
{
    const auto session = sessionFor(connection);
    std::string requestId;
    int groupId = -1;
    if (!session || !session->authenticated)
    {
        sendBusinessResponse(connection, LEAVE_GROUP_MSG_ACK, requestId, false,
                             "UNAUTHENTICATED", "login is required");
        return;
    }
    if (!requestIdField(request, requestId) || !intField(request, "groupid", groupId) ||
        groupId <= 0)
    {
        sendBusinessResponse(connection, LEAVE_GROUP_MSG_ACK, requestId, false,
                             "INVALID_ARGUMENT", "valid request_id and groupid are required");
        return;
    }
    const auto status = groupModel_.leaveGroup(session->userId, groupId);
    if (status != LeaveGroupStatus::Left)
    {
        std::string error = "DATABASE_ERROR";
        std::string message = "group could not be left";
        if (status == LeaveGroupStatus::NotMember)
        {
            error = "NOT_GROUP_MEMBER";
            message = "user is not a member of this group";
        }
        else if (status == LeaveGroupStatus::OwnerCannotLeave)
        {
            error = "GROUP_OWNER_CANNOT_LEAVE";
            message = "the creator cannot leave before ownership transfer is implemented";
        }
        else if (status == LeaveGroupStatus::GroupNotFound)
        {
            error = "GROUP_NOT_FOUND";
            message = "group does not exist";
        }
        sendBusinessResponse(connection, LEAVE_GROUP_MSG_ACK, requestId, false,
                             error, message);
        return;
    }
    sendBusinessResponse(connection, LEAVE_GROUP_MSG_ACK, requestId, true, {}, {},
                         {{"group_id", groupId}});
}

void ChatService::groupChat(const muduo::net::TcpConnectionPtr &connection,
                            json &request, muduo::Timestamp)
{
    const auto session = sessionFor(connection);
    std::string requestId;
    std::string message;
    int groupId = -1;
    if (!session || !session->authenticated)
    {
        sendBusinessResponse(connection, GROUP_CHAT_MSG_ACK, requestId, false,
                             "UNAUTHENTICATED", "login is required");
        return;
    }
    if (!requestIdField(request, requestId) || !intField(request, "groupid", groupId) ||
        groupId <= 0 || !stringField(request, "msg", message) || message.empty() ||
        message.find_first_not_of(" \t\r\n") == std::string::npos ||
        message.size() > 65536)
    {
        sendBusinessResponse(connection, GROUP_CHAT_MSG_ACK, requestId, false,
                             "INVALID_ARGUMENT", "invalid group, request id, or message");
        return;
    }

    bool senderIsMember = false;
    bool querySucceeded = false;
    const auto recipients = groupModel_.queryGroupUsers(
        session->userId, groupId, senderIsMember, querySucceeded);
    if (!querySucceeded || !senderIsMember)
    {
        sendBusinessResponse(connection, GROUP_CHAT_MSG_ACK, requestId, false,
                             querySucceeded ? "NOT_GROUP_MEMBER" : "DATABASE_ERROR",
                             querySucceeded ? "group membership is required"
                                            : "group members could not be loaded");
        return;
    }
    int maxRecipients = environmentInt("CHAT_GROUP_MAX_RECIPIENTS", 256);
    if (maxRecipients < 1 || maxRecipients > 1000) maxRecipients = 256;
    if (recipients.size() > static_cast<std::size_t>(maxRecipients))
    {
        sendBusinessResponse(connection, GROUP_CHAT_MSG_ACK, requestId, false,
                             "GROUP_TOO_LARGE",
                             "group exceeds the configured V1 broadcast limit");
        return;
    }

    json delivery{{"msgid", GROUP_CHAT_MSG}, {"groupid", groupId},
                  {"id", session->userId}, {"name", session->userName},
                  {"msg", message}, {"request_id", requestId},
                  {"time", serverTimeText()}};
    int localCount = 0;
    int remoteCount = 0;
    int offlineCount = 0;
    int failedCount = 0;
    std::vector<std::pair<int, std::string>> offlineDeliveries;
    for (int recipientId : recipients)
    {
        json recipientPayload = delivery;
        recipientPayload["toid"] = recipientId; // internal routing guard
        const std::string payload = recipientPayload.dump();
        const auto local = localConnectionForUser(recipientId);
        if (local)
        {
            nexusim::sendFramed(local, payload);
            ++localCount;
            continue;
        }
        const auto lookup = redis_.lookupPresence(recipientId);
        if (lookup.status == Redis::LookupStatus::Found)
        {
            const auto published = redis_.publishToNode(
                lookup.presence.nodeId,
                {recipientId, lookup.presence.sessionId, payload});
            if (published.status == Redis::PublishStatus::Published)
            {
                ++remoteCount;
                continue;
            }
        }
        // Only the sender node stores offline copies. Subscriber nodes never do.
        offlineDeliveries.emplace_back(recipientId, payload);
    }
    if (!offlineDeliveries.empty())
    {
        if (offlineMessageModel_.insertBatch(offlineDeliveries))
            offlineCount = static_cast<int>(offlineDeliveries.size());
        else
            failedCount = static_cast<int>(offlineDeliveries.size());
    }

    const bool accepted = failedCount == 0;
    json fields{{"group_id", groupId},
                {"recipient_count", static_cast<int>(recipients.size())},
                {"local_queued", localCount}, {"redis_published", remoteCount},
                {"queued_offline", offlineCount}, {"failed", failedCount},
                {"realtime_routed", offlineCount == 0 && failedCount == 0},
                {"delivery_guaranteed", false}};
    sendBusinessResponse(connection, GROUP_CHAT_MSG_ACK, requestId, accepted,
                         accepted ? "" : "GROUP_DELIVERY_FAILED",
                         accepted ? "" : "one or more group messages were not stored",
                         std::move(fields));
    LOG_INFO << "node=" << nodeId_ << " group-chat sender=" << session->userId
             << " group=" << groupId << " local=" << localCount
             << " redis=" << remoteCount << " offline=" << offlineCount
             << " failed=" << failedCount;
}

void ChatService::aiChat(const muduo::net::TcpConnectionPtr &connection,
                         json &request, muduo::Timestamp)
{
    const auto session = sessionFor(connection);
    std::string clientRequestId;
    std::string conversationId;
    std::string prompt;
    if (!session || !session->authenticated)
    {
        sendAiError(connection, request, "UNAUTHENTICATED",
                    "login is required for AI chat");
        return;
    }
    if (!stringField(request, "request_id", clientRequestId) ||
        !validOpaqueId(clientRequestId) ||
        !stringField(request, "conversation_id", conversationId) ||
        !validOpaqueId(conversationId) ||
        !stringField(request, "prompt", prompt) || prompt.empty() ||
        prompt.find_first_not_of(" \t\r\n") == std::string::npos ||
        prompt.size() > 32768)
    {
        sendAiError(connection, request, "AI_INVALID_ARGUMENT",
                    "invalid AI request id, conversation id, or prompt");
        return;
    }

    const auto timeout = aiRpcTimeout();
    const auto deadline = nexusim::PendingRequestRegistry::Clock::now() + timeout;
    const std::string registryRequestId = beginAiRpcRequest(
        *session, clientRequestId, deadline);
    if (registryRequestId.empty())
    {
        sendAiError(connection, request, "AI_SERVICE_BUSY",
                    "duplicate or untrackable AI request");
        return;
    }
    const json timeoutRequest{{"request_id", clientRequestId},
                              {"conversation_id", conversationId}};
    scheduleRpcTimeout(connection, *session, registryRequestId, deadline,
        [this, timeoutRequest](const muduo::net::TcpConnectionPtr &liveConnection) {
            sendAiError(liveConnection, timeoutRequest, "AI_UPSTREAM_TIMEOUT",
                        "AIService RPC deadline exceeded");
        });

    const auto rpcResult = aiServiceClient_.chat(
        session->userId, clientRequestId, conversationId, prompt,
        static_cast<int>(timeout.count()), deadline);
    bool expired = false;
    if (!completeRpcRequest(registryRequestId, *session, &expired) ||
        !isCurrentSession(*session))
    {
        return;
    }
    if (expired)
    {
        sendAiError(connection, timeoutRequest, "AI_UPSTREAM_TIMEOUT",
                    "AIService RPC deadline exceeded");
        return;
    }
    if (!rpcResult.call.ok)
    {
        sendAiError(connection, request, aiRpcErrorName(rpcResult.call.error),
                    rpcResult.call.message.empty()
                        ? "AIService RPC call failed"
                        : rpcResult.call.message);
        return;
    }
    if (rpcResult.statusCode != 0)
    {
        sendAiError(connection, request, nexusim::aiStatusName(rpcResult.statusCode),
                    rpcResult.errorMessage.empty()
                        ? "AIService returned an error"
                        : rpcResult.errorMessage);
        return;
    }
    if (rpcResult.content.empty() || rpcResult.content.size() > 786432)
    {
        sendAiError(connection, request, "AI_INVALID_RESPONSE",
                    "AIService returned empty or oversized content");
        return;
    }

    json response;
    response["msgid"] = AI_CHAT_MSG_ACK;
    response["request_id"] = clientRequestId;
    response["conversation_id"] = conversationId;
    response["ok"] = true;
    response["status_code"] = 0;
    response["content"] = rpcResult.content;
    response["model"] = rpcResult.model;
    response["finish_reason"] = rpcResult.finishReason;
    response["http_status"] = rpcResult.httpStatus;
    if (!rpcResult.upstreamRequestId.empty())
        response["upstream_request_id"] = rpcResult.upstreamRequestId;
    response["usage"] = {{"prompt_tokens", rpcResult.promptTokens},
                         {"completion_tokens", rpcResult.completionTokens},
                         {"total_tokens", rpcResult.totalTokens}};
    if (!rpcResult.errorMessage.empty())
        response["warning"] = rpcResult.errorMessage;
    const std::string responsePayload = response.dump();
    if (responsePayload.size() > nexusim::FrameCodec::kDefaultMaxFrameLength)
    {
        sendAiError(connection, request, "AI_INVALID_RESPONSE",
                    "AI response exceeds the client frame limit");
        return;
    }
    nexusim::sendFramed(connection, responsePayload);
    LOG_INFO << "node=" << nodeId_ << " AI request=" << clientRequestId
             << " user=" << session->userId
             << " http_status=" << rpcResult.httpStatus
             << " total_tokens=" << rpcResult.totalTokens;
}

void ChatService::loginout(const muduo::net::TcpConnectionPtr &connection,
                           json &, muduo::Timestamp)
{
    const auto session = detachConnection(connection);
    if (!session || !session->authenticated)
    {
        sendError(connection, LOGINOUT_MSG, "UNAUTHENTICATED", "login is required");
        return;
    }
    cleanupSession(*session);
    connection->shutdown();
}

muduo::net::TcpConnectionPtr ChatService::localConnectionForUser(
    int userId, const std::string *expectedSessionId) const
{
    std::lock_guard<std::mutex> lock(connectionMutex_);
    const auto connection = userConnections_.find(userId);
    if (connection == userConnections_.end()) return {};
    const auto session = sessions_.find(connection->second->name());
    if (session == sessions_.end() || !session->second.authenticated ||
        session->second.userId != userId ||
        (expectedSessionId != nullptr &&
         session->second.sessionId != *expectedSessionId))
    {
        return {};
    }
    return connection->second;
}

void ChatService::enqueueRedisDelivery(nexusim::ClusterDelivery delivery)
{
    if (!redisInboundExecutor_.trySubmit(
            [this, delivery = std::move(delivery)] { handleRedisDelivery(delivery); }))
    {
        LOG_ERROR << "node=" << nodeId_
                  << " Redis inbound queue full; real-time delivery dropped";
    }
}

void ChatService::handleRedisDelivery(const nexusim::ClusterDelivery &delivery)
{
    try
    {
        const json payload = json::parse(delivery.payload);
        const auto messageId = payload.find("msgid");
        const auto recipient = payload.find("toid");
        if (messageId == payload.end() || !messageId->is_number_integer() ||
            (messageId->get<int>() != ONE_CHAT_MSG &&
             messageId->get<int>() != GROUP_CHAT_MSG) || recipient == payload.end() ||
            !recipient->is_number_integer() || recipient->get<int>() != delivery.recipientId)
        {
            LOG_ERROR << "node=" << nodeId_ << " rejected invalid Redis chat payload";
            return;
        }
    }
    catch (...)
    {
        LOG_ERROR << "node=" << nodeId_ << " rejected malformed Redis chat payload";
        return;
    }

    const auto connection = localConnectionForUser(
        delivery.recipientId, &delivery.expectedSessionId);
    if (!connection)
    {
        LOG_WARN << "node=" << nodeId_ << " discarded stale Redis delivery recipient="
                 << delivery.recipientId;
        return;
    }
    nexusim::sendFramed(connection, delivery.payload);
    LOG_INFO << "node=" << nodeId_ << " accepted Redis delivery recipient="
             << delivery.recipientId;
}

void ChatService::storeOfflineOrReport(
    const muduo::net::TcpConnectionPtr &sender, int recipientId,
    const std::string &payload, const std::string &errorCode,
    const std::string &errorMessage)
{
    const auto session = sessionFor(sender);
    if (!session || !session->authenticated) return;
    const auto deadline = nexusim::PendingRequestRegistry::Clock::now() + rpcTimeout();
    const std::string rpcRequestId = beginRpcRequest(*session, deadline);
    if (rpcRequestId.empty())
    {
        sendError(sender, ONE_CHAT_MSG, "SERVER_BUSY",
                  "could not track recipient query", recipientId, false);
        return;
    }
    scheduleRpcTimeout(sender, *session, rpcRequestId, deadline,
        [this, recipientId](const muduo::net::TcpConnectionPtr &liveConnection) {
            sendError(liveConnection, ONE_CHAT_MSG, "RPC_TIMEOUT",
                      "recipient query deadline exceeded", recipientId, false);
        });

    const auto rpcResult = userServiceClient_.queryUser(recipientId, deadline);
    bool expired = false;
    if (!completeRpcRequest(rpcRequestId, *session, &expired) ||
        !isCurrentSession(*session))
        return;
    if (expired)
    {
        sendError(sender, ONE_CHAT_MSG, "RPC_TIMEOUT",
                  "recipient query deadline exceeded", recipientId, false);
        return;
    }
    if (!rpcResult.call.ok)
    {
        sendError(sender, ONE_CHAT_MSG, rpcErrorName(rpcResult.call.error),
                  rpcResult.call.message, recipientId, false);
        return;
    }
    if (!rpcResult.found)
    {
        sendError(sender, ONE_CHAT_MSG, "RECIPIENT_NOT_FOUND",
                  "recipient does not exist", recipientId, false);
        return;
    }

    const bool stored = offlineMessageModel_.insert(recipientId, payload);
    if (!stored)
    {
        sendError(sender, ONE_CHAT_MSG, "OFFLINE_STORE_FAILED",
                  "message was neither delivered nor stored", recipientId, false);
        LOG_ERROR << "node=" << nodeId_ << " failed to store offline message recipient="
                  << recipientId;
        return;
    }
    if (errorCode != "OFFLINE_STORE_FAILED")
    {
        sendError(sender, ONE_CHAT_MSG, errorCode, errorMessage, recipientId, true);
    }
}

void ChatService::sendError(const muduo::net::TcpConnectionPtr &connection,
                            int requestMsgId, const std::string &code,
                            const std::string &message,
                            std::optional<int> recipientId,
                            std::optional<bool> queuedOffline) const
{
    json response;
    response["msgid"] = ERROR_MSG;
    response["request_msgid"] = requestMsgId;
    response["error"] = code;
    response["errmsg"] = message;
    response["node_id"] = nodeId_;
    if (recipientId) response["toid"] = *recipientId;
    if (queuedOffline) response["queued_offline"] = *queuedOffline;
    nexusim::sendFramed(connection, response.dump());
}

void ChatService::sendAiError(
    const muduo::net::TcpConnectionPtr &connection, const json &request,
    const std::string &code, const std::string &message) const
{
    json response;
    response["msgid"] = AI_CHAT_MSG_ACK;
    response["ok"] = false;
    response["status_code"] = -1;
    response["error"] = code;
    response["errmsg"] = message;
    response["node_id"] = nodeId_;
    const auto requestId = request.find("request_id");
    if (requestId != request.end() && requestId->is_string() &&
        requestId->get_ref<const std::string &>().size() <= 128)
        response["request_id"] = *requestId;
    const auto conversationId = request.find("conversation_id");
    if (conversationId != request.end() && conversationId->is_string() &&
        conversationId->get_ref<const std::string &>().size() <= 128)
        response["conversation_id"] = *conversationId;
    nexusim::sendFramed(connection, response.dump());
}
