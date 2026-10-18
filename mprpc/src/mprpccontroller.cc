#include "mprpccontroller.h"

MprpcController::MprpcController() = default;

void MprpcController::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    failed_ = false;
    errorCode_ = mprpc::RpcErrorCode::Ok;
    errorText_.clear();
    cancelled_.store(false);
    deadline_ = Clock::time_point::max();
    cancelCallbacks_.clear();
}

bool MprpcController::Failed() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return failed_;
}

std::string MprpcController::ErrorText() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return errorText_;
}

void MprpcController::SetFailed(const std::string& reason)
{
    SetFailed(mprpc::RpcErrorCode::InternalError, reason);
}

void MprpcController::SetFailed(mprpc::RpcErrorCode code,
                                const std::string &reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    failed_ = true;
    errorCode_ = code;
    errorText_ = reason;
}

mprpc::RpcErrorCode MprpcController::ErrorCode() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return errorCode_;
}

void MprpcController::StartCancel()
{
    std::vector<google::protobuf::Closure *> callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (cancelled_.exchange(true)) return;
        callbacks.swap(cancelCallbacks_);
    }
    for (auto *callback : callbacks)
    {
        if (callback != nullptr) callback->Run();
    }
}

bool MprpcController::IsCanceled() const
{
    return cancelled_.load();
}

void MprpcController::NotifyOnCancel(google::protobuf::Closure* callback)
{
    if (callback == nullptr) return;
    bool runNow = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        runNow = cancelled_.load();
        if (!runNow) cancelCallbacks_.push_back(callback);
    }
    if (runNow) callback->Run();
}

void MprpcController::SetDeadline(Clock::time_point deadline)
{
    std::lock_guard<std::mutex> lock(mutex_);
    deadline_ = deadline;
}

MprpcController::Clock::time_point MprpcController::Deadline() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return deadline_;
}

bool MprpcController::DeadlineExceeded() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return Clock::now() >= deadline_;
}
