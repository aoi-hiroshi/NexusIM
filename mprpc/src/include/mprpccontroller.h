#pragma once
#include "rpcerror.h"
#include <google/protobuf/service.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

class MprpcController : public google::protobuf::RpcController
{
public:
    MprpcController();
    void Reset() override;
    bool Failed() const override;
    std::string ErrorText() const override;
    void SetFailed(const std::string& reason) override;

    void StartCancel() override;
    bool IsCanceled() const override;
    void NotifyOnCancel(google::protobuf::Closure* callback) override;

    void SetFailed(mprpc::RpcErrorCode code, const std::string &reason);
    mprpc::RpcErrorCode ErrorCode() const;

    using Clock = std::chrono::steady_clock;
    void SetDeadline(Clock::time_point deadline);
    Clock::time_point Deadline() const;
    bool DeadlineExceeded() const;
private:
    mutable std::mutex mutex_;
    bool failed_{false};
    mprpc::RpcErrorCode errorCode_{mprpc::RpcErrorCode::Ok};
    std::string errorText_;
    std::atomic<bool> cancelled_{false};
    Clock::time_point deadline_{Clock::time_point::max()};
    std::vector<google::protobuf::Closure *> cancelCallbacks_;
};
