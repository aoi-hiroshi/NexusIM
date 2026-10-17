#include "server/redis/redis.hpp"

#include <muduo/base/Logging.h>

#include <cstdlib>
#include <sys/socket.h>
#include <stdexcept>
#include <utility>

namespace
{
std::string environmentOr(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    return value == nullptr || *value == '\0' ? std::string(fallback) : std::string(value);
}

redisContext *connectContext(const std::string &host, int port)
{
    timeval timeout{};
    timeout.tv_sec = 3;
    redisContext *context = redisConnectWithTimeout(host.c_str(), port, timeout);
    if (context != nullptr && context->err != 0)
    {
        redisFree(context);
        return nullptr;
    }
    if (context != nullptr) redisSetTimeout(context, timeout);
    return context;
}

bool authenticate(redisContext *context)
{
    const std::string password = environmentOr("CHAT_REDIS_PASSWORD", "");
    if (password.empty()) return true;
    const std::string username = environmentOr("CHAT_REDIS_USERNAME", "");
    redisReply *reply = nullptr;
    if (username.empty())
    {
        reply = static_cast<redisReply *>(redisCommand(
            context, "AUTH %b", password.data(), password.size()));
    }
    else
    {
        reply = static_cast<redisReply *>(redisCommand(
            context, "AUTH %b %b", username.data(), username.size(),
            password.data(), password.size()));
    }
    const bool authenticated = reply != nullptr && reply->type == REDIS_REPLY_STATUS &&
                               std::string(reply->str, reply->len) == "OK";
    if (reply != nullptr) freeReplyObject(reply);
    return authenticated;
}

bool integerReply(redisReply *reply, long long expected)
{
    const bool result = reply != nullptr && reply->type == REDIS_REPLY_INTEGER &&
                        reply->integer == expected;
    if (reply != nullptr) freeReplyObject(reply);
    return result;
}
} // namespace

Redis::Redis()
    : publishContext_(nullptr), subscribeContext_(nullptr), presenceContext_(nullptr),
      stopping_(false), publishHealthy_(false), presenceHealthy_(false),
      subscriptionHealthy_(false)
{
}

Redis::~Redis()
{
    stopping_.store(true);
    if (subscribeContext_ != nullptr && subscribeContext_->fd >= 0)
    {
        ::shutdown(subscribeContext_->fd, SHUT_RDWR);
    }
    if (observerThread_.joinable()) observerThread_.join();
    if (publishContext_ != nullptr) redisFree(publishContext_);
    if (subscribeContext_ != nullptr) redisFree(subscribeContext_);
    if (presenceContext_ != nullptr) redisFree(presenceContext_);
}

bool Redis::connect(const std::string &nodeId)
{
    if (!nexusim::isValidNodeId(nodeId))
    {
        LOG_ERROR << "invalid CHAT_NODE_ID; allowed characters are A-Z, a-z, 0-9, '.', '_' and '-'";
        return false;
    }
    localNodeId_ = nodeId;
    localChannel_ = nexusim::nodeChannel(nodeId);

    const std::string host = environmentOr("CHAT_REDIS_HOST", "127.0.0.1");
    int port = 6379;
    try
    {
        port = std::stoi(environmentOr("CHAT_REDIS_PORT", "6379"));
        if (port <= 0 || port > 65535) throw std::out_of_range("port");
    }
    catch (...)
    {
        LOG_ERROR << "invalid CHAT_REDIS_PORT";
        return false;
    }

    publishContext_ = connectContext(host, port);
    subscribeContext_ = connectContext(host, port);
    presenceContext_ = connectContext(host, port);
    if (publishContext_ == nullptr || subscribeContext_ == nullptr || presenceContext_ == nullptr)
    {
        LOG_ERROR << "node=" << nodeId << " failed to connect all Redis contexts";
        return false;
    }
    if (!authenticate(publishContext_) || !authenticate(subscribeContext_) ||
        !authenticate(presenceContext_))
    {
        LOG_ERROR << "node=" << nodeId << " Redis authentication failed";
        return false;
    }

    redisReply *reply = static_cast<redisReply *>(
        redisCommand(subscribeContext_, "SUBSCRIBE %s", localChannel_.c_str()));
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR)
    {
        if (reply != nullptr) freeReplyObject(reply);
        LOG_ERROR << "node=" << nodeId << " failed to subscribe " << localChannel_;
        return false;
    }
    freeReplyObject(reply);
    publishHealthy_.store(true);
    presenceHealthy_.store(true);
    subscriptionHealthy_.store(true);
    observerThread_ = std::thread([this] { observerChannelMessages(); });
    LOG_INFO << "node=" << nodeId << " subscribed Redis channel=" << localChannel_;
    return true;
}

Redis::PublishResult Redis::publishToNode(
    const std::string &nodeId, const nexusim::ClusterDelivery &delivery)
{
    if (!publishHealthy_.load() || publishContext_ == nullptr ||
        !nexusim::isValidNodeId(nodeId))
    {
        return {};
    }
    const std::string channel = nexusim::nodeChannel(nodeId);
    const std::string envelope = nexusim::encodeDelivery(delivery);
    std::lock_guard<std::mutex> lock(publishMutex_);
    if (!publishHealthy_.load()) return {};
    redisReply *reply = static_cast<redisReply *>(redisCommand(
        publishContext_, "PUBLISH %s %b", channel.c_str(),
        envelope.data(), envelope.size()));
    if (reply == nullptr || reply->type != REDIS_REPLY_INTEGER)
    {
        if (reply != nullptr) freeReplyObject(reply);
        publishHealthy_.store(false);
        LOG_ERROR << "node=" << localNodeId_ << " Redis publish connection failed";
        return {};
    }
    const long long subscribers = reply->integer;
    freeReplyObject(reply);
    return {subscribers > 0 ? PublishStatus::Published : PublishStatus::NoSubscribers,
            subscribers};
}

Redis::LookupResult Redis::lookupPresence(int userId)
{
    if (!presenceHealthy_.load() || presenceContext_ == nullptr || userId <= 0)
    {
        return {};
    }
    const std::string key = presenceKey(userId);
    std::lock_guard<std::mutex> lock(presenceMutex_);
    if (!presenceHealthy_.load()) return {};
    redisReply *reply = static_cast<redisReply *>(
        redisCommand(presenceContext_, "GET %s", key.c_str()));
    if (reply == nullptr)
    {
        presenceHealthy_.store(false);
        LOG_ERROR << "node=" << localNodeId_ << " Redis Presence connection failed";
        return {};
    }
    if (reply->type == REDIS_REPLY_NIL)
    {
        freeReplyObject(reply);
        return {LookupStatus::NotFound, {}};
    }
    if (reply->type != REDIS_REPLY_STRING)
    {
        freeReplyObject(reply);
        return {};
    }
    const std::string value(reply->str, reply->len);
    freeReplyObject(reply);
    const auto parsed = nexusim::decodePresence(value);
    if (!parsed)
    {
        LOG_ERROR << "node=" << localNodeId_ << " found malformed Presence key=" << key;
        return {};
    }
    return {LookupStatus::Found, *parsed};
}

Redis::AcquireStatus Redis::acquirePresence(int userId, const std::string &nodeId,
                                            const std::string &connectionId,
                                            const std::string &sessionId,
                                            int ttlSeconds)
{
    if (!presenceHealthy_.load() || presenceContext_ == nullptr || userId <= 0 ||
        ttlSeconds <= 0 || !nexusim::isValidNodeId(nodeId) ||
        !nexusim::isValidConnectionId(connectionId) ||
        !nexusim::isValidSessionId(sessionId))
        return AcquireStatus::Error;
    const std::string key = presenceKey(userId);
    const std::string value = nexusim::encodePresence({nodeId, connectionId, sessionId});
    std::lock_guard<std::mutex> lock(presenceMutex_);
    if (!presenceHealthy_.load()) return AcquireStatus::Error;
    redisReply *reply = static_cast<redisReply *>(redisCommand(
        presenceContext_, "SET %s %b NX EX %d", key.c_str(), value.data(),
        value.size(), ttlSeconds));
    if (reply == nullptr)
    {
        presenceHealthy_.store(false);
        LOG_ERROR << "node=" << localNodeId_ << " Redis Presence acquire failed";
        return AcquireStatus::Error;
    }
    AcquireStatus status = AcquireStatus::Error;
    if (reply->type == REDIS_REPLY_STATUS &&
        std::string(reply->str, reply->len) == "OK")
    {
        status = AcquireStatus::Acquired;
    }
    else if (reply->type == REDIS_REPLY_NIL)
    {
        status = AcquireStatus::AlreadyOwned;
    }
    else
    {
        LOG_ERROR << "node=" << localNodeId_ << " Redis Presence acquire returned an error";
    }
    freeReplyObject(reply);
    return status;
}

bool Redis::renewPresence(int userId, const std::string &nodeId,
                          const std::string &connectionId,
                          const std::string &sessionId, int ttlSeconds)
{
    if (!presenceHealthy_.load() || presenceContext_ == nullptr || userId <= 0 ||
        ttlSeconds <= 0 || !nexusim::isValidNodeId(nodeId) ||
        !nexusim::isValidConnectionId(connectionId) ||
        !nexusim::isValidSessionId(sessionId))
        return false;
    static const char script[] =
        "if redis.call('GET',KEYS[1]) == ARGV[1] then "
        "return redis.call('EXPIRE',KEYS[1],ARGV[2]) else return 0 end";
    const std::string key = presenceKey(userId);
    const std::string value = nexusim::encodePresence({nodeId, connectionId, sessionId});
    std::lock_guard<std::mutex> lock(presenceMutex_);
    if (!presenceHealthy_.load()) return false;
    redisReply *reply = static_cast<redisReply *>(redisCommand(
        presenceContext_, "EVAL %s 1 %s %b %d", script, key.c_str(),
        value.data(), value.size(), ttlSeconds));
    if (reply == nullptr)
    {
        presenceHealthy_.store(false);
        LOG_ERROR << "node=" << localNodeId_ << " Redis Presence renewal failed";
        return false;
    }
    return integerReply(reply, 1);
}

bool Redis::releasePresence(int userId, const std::string &nodeId,
                            const std::string &connectionId,
                            const std::string &sessionId)
{
    if (!presenceHealthy_.load() || presenceContext_ == nullptr || userId <= 0 ||
        !nexusim::isValidNodeId(nodeId) ||
        !nexusim::isValidConnectionId(connectionId) ||
        !nexusim::isValidSessionId(sessionId))
        return false;
    static const char script[] =
        "if redis.call('GET',KEYS[1]) == ARGV[1] then "
        "return redis.call('DEL',KEYS[1]) else return 0 end";
    const std::string key = presenceKey(userId);
    const std::string value = nexusim::encodePresence({nodeId, connectionId, sessionId});
    std::lock_guard<std::mutex> lock(presenceMutex_);
    if (!presenceHealthy_.load()) return false;
    redisReply *reply = static_cast<redisReply *>(redisCommand(
        presenceContext_, "EVAL %s 1 %s %b", script, key.c_str(),
        value.data(), value.size()));
    if (reply == nullptr)
    {
        presenceHealthy_.store(false);
        LOG_ERROR << "node=" << localNodeId_ << " Redis Presence release failed; TTL will clean up";
        return false;
    }
    return integerReply(reply, 1);
}

void Redis::initNotifyHandler(DeliveryHandler handler)
{
    std::lock_guard<std::mutex> lock(handlerMutex_);
    notifyHandler_ = std::move(handler);
}

bool Redis::subscriptionHealthy() const noexcept
{
    return subscriptionHealthy_.load();
}

void Redis::observerChannelMessages()
{
    redisReply *reply = nullptr;
    while (!stopping_.load() &&
           redisGetReply(subscribeContext_, reinterpret_cast<void **>(&reply)) == REDIS_OK)
    {
        if (reply != nullptr && reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3 &&
            reply->element[2] != nullptr && reply->element[2]->str != nullptr)
        {
            const std::string envelope(reply->element[2]->str, reply->element[2]->len);
            const auto delivery = nexusim::decodeDelivery(envelope);
            if (delivery)
            {
                DeliveryHandler handler;
                {
                    std::lock_guard<std::mutex> lock(handlerMutex_);
                    handler = notifyHandler_;
                }
                if (handler) handler(*delivery);
            }
            else
            {
                LOG_ERROR << "node=" << localNodeId_ << " ignored malformed cluster delivery";
            }
        }
        if (reply != nullptr)
        {
            freeReplyObject(reply);
            reply = nullptr;
        }
    }
    if (reply != nullptr) freeReplyObject(reply);
    subscriptionHealthy_.store(false);
    if (!stopping_.load())
    {
        LOG_ERROR << "node=" << localNodeId_ << " Redis subscription disconnected; restart required";
    }
}

std::string Redis::presenceKey(int userId) const
{
    return "nexusim:presence:user:" + std::to_string(userId);
}
