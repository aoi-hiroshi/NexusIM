#include "server/concurrency/connectionstrand.hpp"

#include <stdexcept>
#include <utility>

namespace nexusim
{

ConnectionStrand::ConnectionStrand(std::size_t workerCount,
                                   std::size_t runnableLaneCapacity,
                                   std::size_t perConnectionCapacity)
    : executor_("connection-strand", workerCount, runnableLaneCapacity),
      perConnectionCapacity_(perConnectionCapacity)
{
    if (perConnectionCapacity_ == 0)
    {
        throw std::invalid_argument("per-connection capacity must be positive");
    }
}

ConnectionStrand::~ConnectionStrand()
{
    shutdown();
}

bool ConnectionStrand::trySubmit(const std::string &connectionId, Task task)
{
    if (connectionId.empty() || !task)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_)
    {
        return false;
    }

    auto &lane = lanes_[connectionId];
    if (lane.cancelled)
    {
        return false;
    }
    if (lane.tasks.size() >= perConnectionCapacity_)
    {
        return false;
    }

    lane.tasks.push_back(std::move(task));
    if (lane.running)
    {
        return true;
    }

    lane.running = true;
    if (executor_.trySubmit([this, connectionId] { drain(connectionId); }))
    {
        return true;
    }

    lanes_.erase(connectionId);
    return false;
}

std::size_t ConnectionStrand::cancel(const std::string &connectionId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = lanes_.find(connectionId);
    if (it == lanes_.end())
    {
        return 0;
    }

    const std::size_t cancelled = it->second.tasks.size();
    it->second.tasks.clear();
    it->second.cancelled = true;
    if (!it->second.running)
    {
        lanes_.erase(it);
    }
    return cancelled;
}

void ConnectionStrand::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_)
        {
            return;
        }
        stopping_ = true;
        for (auto &entry : lanes_)
        {
            entry.second.cancelled = true;
            entry.second.tasks.clear();
        }
    }

    executor_.shutdown();

    std::lock_guard<std::mutex> lock(mutex_);
    lanes_.clear();
}

void ConnectionStrand::drain(const std::string &connectionId)
{
    for (;;)
    {
        Task task;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = lanes_.find(connectionId);
            if (it == lanes_.end())
            {
                return;
            }

            auto &lane = it->second;
            if (lane.cancelled || lane.tasks.empty())
            {
                lanes_.erase(it);
                return;
            }

            task = std::move(lane.tasks.front());
            lane.tasks.pop_front();
        }

        try
        {
            task();
        }
        catch (...)
        {
            // Continue draining this connection after an isolated task failure.
        }
    }
}

} // namespace nexusim

