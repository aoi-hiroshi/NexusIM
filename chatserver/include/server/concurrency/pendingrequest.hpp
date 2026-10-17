#ifndef NEXUSIM_PENDINGREQUEST_H
#define NEXUSIM_PENDINGREQUEST_H

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexusim
{

class PendingRequestRegistry
{
public:
    using Clock = std::chrono::steady_clock;

    struct Request
    {
        std::string requestId;
        std::string connectionId;
        std::string sessionId;
        Clock::time_point deadline;
    };

    enum class Completion
    {
        Completed,
        NotFound,
        Expired,
        SessionMismatch
    };

    bool add(Request request);
    Completion complete(const std::string &requestId,
                        const std::string &connectionId,
                        const std::string &sessionId,
                        Clock::time_point now = Clock::now());
    bool cancel(const std::string &requestId);
    std::size_t cancelConnection(const std::string &connectionId,
                                 const std::string &sessionId);
    std::vector<std::string> expire(Clock::time_point now = Clock::now());
    std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Request> requests_;
};

} // namespace nexusim

#endif

