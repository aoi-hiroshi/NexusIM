#ifndef REDIS_H
#define REDIS_H

#include <hiredis/hiredis.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// These synchronous operations belong only on bounded worker threads.
class Redis
{
public:
    Redis();
    ~Redis();

    Redis(const Redis &) = delete;
    Redis &operator=(const Redis &) = delete;

    bool connect();
    bool publish(int recipientId, const std::string &message);

    bool acquirePresence(int userId, const std::string &nodeId,
                         const std::string &connectionId,
                         const std::string &sessionId, int ttlSeconds);
    bool renewPresence(int userId, const std::string &nodeId,
                       const std::string &connectionId,
                       const std::string &sessionId, int ttlSeconds);
    bool releasePresence(int userId, const std::string &nodeId,
                         const std::string &connectionId,
                         const std::string &sessionId);
    bool isUserPresent(int userId);

    void init_notify_handler(std::function<void(int, std::string)> handler);

private:
    void observerChannelMessages();
    std::string presenceKey(int userId) const;
    std::string presenceValue(const std::string &nodeId,
                              const std::string &connectionId,
                              const std::string &sessionId) const;

    redisContext *publishContext_;
    redisContext *subscribeContext_;
    redisContext *presenceContext_;
    std::mutex publishMutex_;
    std::mutex presenceMutex_;
    std::thread observerThread_;
    std::atomic<bool> stopping_;
    std::function<void(int, std::string)> notifyHandler_;
    std::mutex handlerMutex_;
    std::string clusterChannel_;
};

#endif
