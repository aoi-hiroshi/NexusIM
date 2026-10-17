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
      presenceTtlSeconds_(environmentInt("CHAT_PRESENCE_TTL_SECONDS", 90))
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

    if (redis_.connect())
    {
        redis_.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage,
                                             this, std::placeholders::_1,
                                             std::placeholders::_2));
    }
    else
    {
        LOG_ERROR << "Redis unavailable; login fails closed because presence cannot be acquired";
    }
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
        userModel_.updateState(user);
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

    const std::string sessionId = nexusim::generateSessionId();
    if (!redis_.acquirePresence(id, nodeId_, connection->name(), sessionId,
                                presenceTtlSeconds_))
    {
        response["errno"] = 2;
        response["errmsg"] = "account is already online or presence service is unavailable";
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
    userModel_.updateState(user);
    response["errno"] = 0;
    response["id"] = user.getId();
    response["name"] = user.getName();
    response["session_id"] = sessionId;

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
}

void ChatService::reg(const muduo::net::TcpConnectionPtr &connection,
                      json &request, muduo::Timestamp)
{
    std::string name;
    std::string password;
    if (!stringField(request, "name", name) || !stringField(request, "password", password) ||
        name.empty() || name.size() > 64 || password.size() < 8 || password.size() > 128)
    {
        sendError(connection, REG_MSG, "INVALID_ARGUMENT",
                  "name is required and password must contain 8-128 bytes");
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

    muduo::net::TcpConnectionPtr localRecipient;
    {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        const auto iterator = userConnections_.find(recipientId);
        if (iterator != userConnections_.end()) localRecipient = iterator->second;
    }
    if (localRecipient)
    {
        nexusim::sendFramed(localRecipient, payload);
    }
    else if (redis_.isUserPresent(recipientId))
    {
        if (!redis_.publish(recipientId, payload))
        {
            offlineMessageModel_.insert(recipientId, payload);
        }
    }
    else
    {
        offlineMessageModel_.insert(recipientId, payload);
    }
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

void ChatService::reset()
{
    userModel_.resetState();
}

void ChatService::handleRedisSubscribeMessage(int userId, std::string message)
{
    muduo::net::TcpConnectionPtr connection;
    {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        const auto iterator = userConnections_.find(userId);
        if (iterator != userConnections_.end()) connection = iterator->second;
    }
    // Every node receives the cluster channel. Only the owner forwards it; the
    // publisher is responsible for offline fallback on publish failure.
    if (connection) nexusim::sendFramed(connection, message);
}

void ChatService::sendError(const muduo::net::TcpConnectionPtr &connection,
                            int requestMsgId, const std::string &code,
                            const std::string &message) const
{
    json response;
    response["msgid"] = ERROR_MSG;
    response["request_msgid"] = requestMsgId;
    response["error"] = code;
    response["errmsg"] = message;
    nexusim::sendFramed(connection, response.dump());
}
