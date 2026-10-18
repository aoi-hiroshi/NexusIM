#pragma once

#include "mprpccontroller.h"

#include <chrono>
#include <string>

namespace nexusim
{

struct RpcCallStatus
{
    bool ok{false};
    mprpc::RpcErrorCode error{mprpc::RpcErrorCode::Ok};
    std::string message;
};

struct LoginRpcResult
{
    RpcCallStatus call;
    bool authenticated{false};
    int userId{-1};
    std::string username;
    int userError{0};
    std::string userMessage;
};

struct RegisterRpcResult
{
    RpcCallStatus call;
    bool registered{false};
    int userId{-1};
    std::string username;
    int userError{0};
    std::string userMessage;
};

struct QueryUserRpcResult
{
    RpcCallStatus call;
    bool found{false};
    int userId{-1};
    std::string username;
    int userError{0};
    std::string userMessage;
};

class UserServiceClient
{
public:
    using Deadline = MprpcController::Clock::time_point;

    LoginRpcResult login(int userId, const std::string &password,
                         Deadline deadline) const;
    RegisterRpcResult registerUser(const std::string &username,
                                   const std::string &password,
                                   Deadline deadline) const;
    QueryUserRpcResult queryUser(int userId, Deadline deadline) const;
};

} // namespace nexusim
