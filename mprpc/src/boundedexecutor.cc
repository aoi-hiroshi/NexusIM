#include "boundedexecutor.h"

#include <stdexcept>
#include <utility>

namespace mprpc
{

BoundedExecutor::BoundedExecutor(std::string name, std::size_t threadCount,
                                 std::size_t queueCapacity)
    : name_(std::move(name)), queueCapacity_(queueCapacity)
{
    if (threadCount == 0 || queueCapacity_ == 0)
    {
        throw std::invalid_argument("RPC executor size and capacity must be positive");
    }
    workers_.reserve(threadCount);
    for (std::size_t index = 0; index < threadCount; ++index)
    {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

BoundedExecutor::~BoundedExecutor()
{
    shutdown();
}

bool BoundedExecutor::trySubmit(Task task)
{
    if (!task) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || tasks_.size() >= queueCapacity_) return false;
        tasks_.push_back(std::move(task));
    }
    taskAvailable_.notify_one();
    return true;
}

void BoundedExecutor::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    taskAvailable_.notify_all();
    for (auto &worker : workers_)
    {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
}

void BoundedExecutor::workerLoop()
{
    for (;;)
    {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            taskAvailable_.wait(lock, [this] {
                return stopping_ || !tasks_.empty();
            });
            if (stopping_ && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        try
        {
            task();
        }
        catch (...)
        {
            // Isolate a failed service call from the provider worker pool.
        }
    }
}

} // namespace mprpc
