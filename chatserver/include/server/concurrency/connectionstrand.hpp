#ifndef NEXUSIM_CONNECTIONSTRAND_H
#define NEXUSIM_CONNECTIONSTRAND_H

#include "server/concurrency/boundedexecutor.hpp"

#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace nexusim
{

class ConnectionStrand
{
public:
    using Task = std::function<void()>;

    ConnectionStrand(std::size_t workerCount,
                     std::size_t runnableLaneCapacity,
                     std::size_t perConnectionCapacity);
    ~ConnectionStrand();

    ConnectionStrand(const ConnectionStrand &) = delete;
    ConnectionStrand &operator=(const ConnectionStrand &) = delete;

    bool trySubmit(const std::string &connectionId, Task task);
    std::size_t cancel(const std::string &connectionId);
    void shutdown();

private:
    struct Lane
    {
        std::deque<Task> tasks;
        bool running{false};
        bool cancelled{false};
    };

    void drain(const std::string &connectionId);

    BoundedExecutor executor_;
    const std::size_t perConnectionCapacity_;
    std::mutex mutex_;
    std::unordered_map<std::string, Lane> lanes_;
    bool stopping_{false};
};

} // namespace nexusim

#endif

