#include "server/concurrency/pendingrequest.hpp"

#include <utility>

namespace nexusim
{

bool PendingRequestRegistry::add(Request request)
{
    if (request.requestId.empty() || request.connectionId.empty() || request.sessionId.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    return requests_.emplace(request.requestId, std::move(request)).second;
}

PendingRequestRegistry::Completion PendingRequestRegistry::complete(
    const std::string &requestId,
    const std::string &connectionId,
    const std::string &sessionId,
    Clock::time_point now)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = requests_.find(requestId);
    if (it == requests_.end())
    {
        return Completion::NotFound;
    }
    if (it->second.connectionId != connectionId || it->second.sessionId != sessionId)
    {
        return Completion::SessionMismatch;
    }
    if (now > it->second.deadline)
    {
        requests_.erase(it);
        return Completion::Expired;
    }

    requests_.erase(it);
    return Completion::Completed;
}

bool PendingRequestRegistry::cancel(const std::string &requestId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_.erase(requestId) != 0;
}

std::size_t PendingRequestRegistry::cancelConnection(const std::string &connectionId,
                                                     const std::string &sessionId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t cancelled = 0;
    for (auto it = requests_.begin(); it != requests_.end();)
    {
        if (it->second.connectionId == connectionId && it->second.sessionId == sessionId)
        {
            it = requests_.erase(it);
            ++cancelled;
        }
        else
        {
            ++it;
        }
    }
    return cancelled;
}

std::vector<std::string> PendingRequestRegistry::expire(Clock::time_point now)
{
    std::vector<std::string> expired;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = requests_.begin(); it != requests_.end();)
    {
        if (now > it->second.deadline)
        {
            expired.push_back(it->first);
            it = requests_.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return expired;
}

std::size_t PendingRequestRegistry::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return requests_.size();
}

} // namespace nexusim

