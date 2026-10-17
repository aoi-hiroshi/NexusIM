#include "redis.hpp"

#include <cstdlib>
#include <iostream>
#include <sys/socket.h>
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
    if (context != nullptr)
    {
        redisSetTimeout(context, timeout);
    }
    return context;
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
      stopping_(false), clusterChannel_(environmentOr("CHAT_REDIS_CHANNEL", "nexusim:messages"))
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

bool Redis::connect()
{
    const std::string host = environmentOr("CHAT_REDIS_HOST", "127.0.0.1");
    int port = 6379;
    try
    {
        port = std::stoi(environmentOr("CHAT_REDIS_PORT", "6379"));
    }
    catch (...)
    {
        std::cerr << "invalid CHAT_REDIS_PORT" << std::endl;
        return false;
    }

    publishContext_ = connectContext(host, port);
    subscribeContext_ = connectContext(host, port);
    presenceContext_ = connectContext(host, port);
    if (publishContext_ == nullptr || subscribeContext_ == nullptr || presenceContext_ == nullptr)
    {
        std::cerr << "connect redis failed" << std::endl;
        return false;
    }

    redisReply *reply = static_cast<redisReply *>(
        redisCommand(subscribeContext_, "SUBSCRIBE %s", clusterChannel_.c_str()));
    if (reply == nullptr || reply->type == REDIS_REPLY_ERROR)
    {
        if (reply != nullptr) freeReplyObject(reply);
        std::cerr << "subscribe cluster channel failed" << std::endl;
        return false;
    }
    freeReplyObject(reply);
    observerThread_ = std::thread([this] { observerChannelMessages(); });
    return true;
}

bool Redis::publish(int recipientId, const std::string &message)
{
    if (publishContext_ == nullptr) return false;
    const std::string envelope = std::to_string(recipientId) + "\n" + message;
    std::lock_guard<std::mutex> lock(publishMutex_);
    redisReply *reply = static_cast<redisReply *>(redisCommand(
        publishContext_, "PUBLISH %s %b", clusterChannel_.c_str(),
        envelope.data(), envelope.size()));
    if (reply == nullptr) return false;
    freeReplyObject(reply);
    return true;
}

bool Redis::acquirePresence(int userId, const std::string &nodeId,
                            const std::string &connectionId,
                            const std::string &sessionId, int ttlSeconds)
{
    if (presenceContext_ == nullptr || ttlSeconds <= 0) return false;
    const std::string key = presenceKey(userId);
    const std::string value = presenceValue(nodeId, connectionId, sessionId);
    std::lock_guard<std::mutex> lock(presenceMutex_);
    redisReply *reply = static_cast<redisReply *>(redisCommand(
        presenceContext_, "SET %s %b NX EX %d", key.c_str(), value.data(),
        value.size(), ttlSeconds));
    const bool ok = reply != nullptr && reply->type == REDIS_REPLY_STATUS &&
                    std::string(reply->str, reply->len) == "OK";
    if (reply != nullptr) freeReplyObject(reply);
    return ok;
}

bool Redis::renewPresence(int userId, const std::string &nodeId,
                          const std::string &connectionId,
                          const std::string &sessionId, int ttlSeconds)
{
    if (presenceContext_ == nullptr || ttlSeconds <= 0) return false;
    static const char script[] =
        "if redis.call('GET',KEYS[1]) == ARGV[1] then "
        "return redis.call('EXPIRE',KEYS[1],ARGV[2]) else return 0 end";
    const std::string key = presenceKey(userId);
    const std::string value = presenceValue(nodeId, connectionId, sessionId);
    std::lock_guard<std::mutex> lock(presenceMutex_);
    redisReply *reply = static_cast<redisReply *>(redisCommand(
        presenceContext_, "EVAL %s 1 %s %b %d", script, key.c_str(),
        value.data(), value.size(), ttlSeconds));
    return integerReply(reply, 1);
}

bool Redis::releasePresence(int userId, const std::string &nodeId,
                            const std::string &connectionId,
                            const std::string &sessionId)
{
    if (presenceContext_ == nullptr) return false;
    static const char script[] =
        "if redis.call('GET',KEYS[1]) == ARGV[1] then "
        "return redis.call('DEL',KEYS[1]) else return 0 end";
    const std::string key = presenceKey(userId);
    const std::string value = presenceValue(nodeId, connectionId, sessionId);
    std::lock_guard<std::mutex> lock(presenceMutex_);
    redisReply *reply = static_cast<redisReply *>(redisCommand(
        presenceContext_, "EVAL %s 1 %s %b", script, key.c_str(),
        value.data(), value.size()));
    return integerReply(reply, 1);
}

bool Redis::isUserPresent(int userId)
{
    if (presenceContext_ == nullptr) return false;
    const std::string key = presenceKey(userId);
    std::lock_guard<std::mutex> lock(presenceMutex_);
    redisReply *reply = static_cast<redisReply *>(
        redisCommand(presenceContext_, "EXISTS %s", key.c_str()));
    return integerReply(reply, 1);
}

void Redis::init_notify_handler(std::function<void(int, std::string)> handler)
{
    std::lock_guard<std::mutex> lock(handlerMutex_);
    notifyHandler_ = std::move(handler);
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
            const auto separator = envelope.find('\n');
            if (separator != std::string::npos)
            {
                try
                {
                    const int recipientId = std::stoi(envelope.substr(0, separator));
                    std::function<void(int, std::string)> handler;
                    {
                        std::lock_guard<std::mutex> lock(handlerMutex_);
                        handler = notifyHandler_;
                    }
                    if (handler) handler(recipientId, envelope.substr(separator + 1));
                }
                catch (...)
                {
                    std::cerr << "ignored malformed redis message envelope" << std::endl;
                }
            }
        }
        if (reply != nullptr)
        {
            freeReplyObject(reply);
            reply = nullptr;
        }
    }
}

std::string Redis::presenceKey(int userId) const
{
    return "nexusim:presence:user:" + std::to_string(userId);
}

std::string Redis::presenceValue(const std::string &nodeId,
                                 const std::string &connectionId,
                                 const std::string &sessionId) const
{
    return nodeId + "|" + connectionId + "|" + sessionId;
}
