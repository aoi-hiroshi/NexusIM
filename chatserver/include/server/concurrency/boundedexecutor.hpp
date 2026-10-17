#ifndef NEXUSIM_BOUNDEDEXECUTOR_H
#define NEXUSIM_BOUNDEDEXECUTOR_H

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nexusim
{

class BoundedExecutor
{
public:
    using Task = std::function<void()>;

    BoundedExecutor(std::string name, std::size_t threadCount, std::size_t queueCapacity);
    ~BoundedExecutor();

    BoundedExecutor(const BoundedExecutor &) = delete;
    BoundedExecutor &operator=(const BoundedExecutor &) = delete;

    bool trySubmit(Task task);
    void shutdown();
    std::size_t queuedTaskCount() const;

private:
    void workerLoop();

    const std::string name_;
    const std::size_t queueCapacity_;
    mutable std::mutex mutex_;
    std::condition_variable taskAvailable_;
    std::deque<Task> tasks_;
    std::vector<std::thread> workers_;
    bool stopping_{false};
};

} // namespace nexusim

#endif

