#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mprpc
{

class BoundedExecutor
{
public:
    using Task = std::function<void()>;

    BoundedExecutor(std::string name, std::size_t threadCount,
                    std::size_t queueCapacity);
    ~BoundedExecutor();

    BoundedExecutor(const BoundedExecutor &) = delete;
    BoundedExecutor &operator=(const BoundedExecutor &) = delete;

    bool trySubmit(Task task);
    void shutdown();

private:
    void workerLoop();

    std::string name_;
    std::size_t queueCapacity_;
    std::mutex mutex_;
    std::condition_variable taskAvailable_;
    std::deque<Task> tasks_;
    std::vector<std::thread> workers_;
    bool stopping_{false};
};

} // namespace mprpc
