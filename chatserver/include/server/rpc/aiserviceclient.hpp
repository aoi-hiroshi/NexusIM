#pragma once

#include "mprpccontroller.h"
#include "server/rpc/userserviceclient.hpp"

#include <chrono>
#include <cstdint>
#include <string>

namespace nexusim
{

struct AIChatRpcResult
{
    RpcCallStatus call;
    int statusCode{0};
    std::string content;
    std::string model;
    std::string errorMessage;
    std::string finishReason;
    std::string upstreamRequestId;
    int httpStatus{0};
    std::int64_t promptTokens{0};
    std::int64_t completionTokens{0};
    std::int64_t totalTokens{0};
};

const char *aiStatusName(int statusCode) noexcept;

class AIServiceClient
{
public:
    using Deadline = MprpcController::Clock::time_point;

    AIChatRpcResult chat(int userId, const std::string &requestId,
                         const std::string &conversationId,
                         const std::string &prompt, int deadlineMs,
                         Deadline deadline) const;
};

} // namespace nexusim
