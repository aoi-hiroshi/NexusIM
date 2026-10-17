#ifndef REDIS_H
#define REDIS_H

#include "server/redis/clusterprotocol.hpp"

#include <hiredis/hiredis.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// All command methods are synchronous and must run on bounded workers, never
// in a Muduo EventLoop callback. The subscription connection is read-only.
class Redis
{
public:
    enum class LookupStatus
    {
        Found,
        NotFound,
        Error
    };

    struct LookupResult
    {
        LookupStatus status{LookupStatus::Error};
        nexusim::PresenceRecord presence;
    };

    enum class PublishStatus
    {
        Published,
        NoSubscribers,
        Error
    };

    struct PublishResult
    {
        PublishStatus status{PublishStatus::Error};
        long long subscriberCount{0};
    };

    enum class AcquireStatus
    {
        Acquired,
        AlreadyOwned,
        Error
    };

    using DeliveryHandler = std::function<void(nexusim::ClusterDelivery)>;

    Redis();
    ~Redis();

    Redis(const Redis &) = delete;
    Redis &operator=(const Redis &) = delete;

    bool connect(const std::string &nodeId);
    PublishResult publishToNode(const std::string &nodeId,
                                const nexusim::ClusterDelivery &delivery);
    LookupResult lookupPresence(int userId);

    AcquireStatus acquirePresence(int userId, const std::string &nodeId,
                                  const std::string &connectionId,
                                  const std::string &sessionId, int ttlSeconds);
    bool renewPresence(int userId, const std::string &nodeId,
                       const std::string &connectionId,
                       const std::string &sessionId, int ttlSeconds);
    bool releasePresence(int userId, const std::string &nodeId,
                         const std::string &connectionId,
                         const std::string &sessionId);

    void initNotifyHandler(DeliveryHandler handler);
    bool subscriptionHealthy() const noexcept;

private:
    void observerChannelMessages();
    std::string presenceKey(int userId) const;

    redisContext *publishContext_;
    redisContext *subscribeContext_;
    redisContext *presenceContext_;
    std::mutex publishMutex_;
    std::mutex presenceMutex_;
    std::thread observerThread_;
    std::atomic<bool> stopping_;
    std::atomic<bool> publishHealthy_;
    std::atomic<bool> presenceHealthy_;
    std::atomic<bool> subscriptionHealthy_;
    DeliveryHandler notifyHandler_;
    std::mutex handlerMutex_;
    std::string localNodeId_;
    std::string localChannel_;
};

#endif
