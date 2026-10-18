#include "server/chatservice.hpp"

#include "public.hpp"
#include "server/security/passwordhasher.hpp"
#include "server/wireprotocol.hpp"

#include <muduo/base/Logging.h>

#include <cstdlib>
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
    // V1 allowlist. Friend/group handlers remain compiled for later migration but
    // are deliberately unreachable over the network.
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
    if (redis_.releasePresence(session.userId, nodeId_, session.connectionId,
                               session.sessionId))
    {
        User user(session.userId, "", "", "offline");
        if (!userModel_.updateState(user))
        {
            LOG_ERROR << "node=" << nodeId_ << " failed to persist offline state user="
                      << session.userId;
        }
    }
    else
    {
        LOG_WARN << "node=" << nodeId_ << " did not release Presence for user="
                 << session.userId << "; key is stale, unavailable, or owned by a newer session";
    }
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

    User user = userModel_.query(id);
    bool passwordMatches = user.getId() == id &&
                           nexusim::PasswordHasher::verify(password, user.getPwd());
    if (!passwordMatches && user.getId() == id &&
        !nexusim::PasswordHasher::isEncoded(user.getPwd()) && user.getPwd() == password)
    {
        const std::string migrated = nexusim::PasswordHasher::hash(password);
        passwordMatches = userModel_.updatePassword(id, migrated);
    }

    json response;
    response["msgid"] = LOGIN_MSG_ACK;
    if (!passwordMatches)
    {
        response["errno"] = 1;
        response["errmsg"] = "id or password is invalid";
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
    if (!bindAuthenticated(connection, id, user.getName(), sessionId))
    {
        redis_.releasePresence(id, nodeId_, connection->name(), sessionId);
        response["errno"] = 3;
        response["errmsg"] = "connection closed or login state changed";
        nexusim::sendFramed(connection, response.dump());
        return;
    }

    user.setState("online");
    if (!userModel_.updateState(user))
    {
        const auto boundSession = sessionFor(connection);
        if (boundSession) invalidateSession(*boundSession);
        redis_.releasePresence(id, nodeId_, connection->name(), sessionId);
        response["errno"] = 5;
        response["errmsg"] = "database state update failed";
        nexusim::sendFramedAndShutdown(connection, response.dump());
        return;
    }
    response["errno"] = 0;
    response["id"] = user.getId();
    response["name"] = user.getName();
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

    User user;
    user.setName(name);
    user.setPwd(nexusim::PasswordHasher::hash(password));
    json response;
    response["msgid"] = REG_MSG_ACK;
    if (userModel_.insert(user))
    {
        response["errno"] = 0;
        response["id"] = user.getId();
    }
    else
    {
        response["errno"] = 1;
        response["errmsg"] = "registration failed";
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
            messageId->get<int>() != ONE_CHAT_MSG || recipient == payload.end() ||
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
    bool recipientCheckSucceeded = false;
    if (!userModel_.exists(recipientId, recipientCheckSucceeded))
    {
        sendError(sender, ONE_CHAT_MSG,
                  recipientCheckSucceeded ? "RECIPIENT_NOT_FOUND"
                                          : "RECIPIENT_CHECK_FAILED",
                  recipientCheckSucceeded ? "recipient does not exist"
                                          : "recipient could not be validated",
                  recipientId, false);
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
