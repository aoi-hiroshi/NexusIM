#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nexusim
{
namespace ai_service
{

enum class AIStatus
{
    Ok = 0,
    InvalidArgument,
    Busy,
    ConfigMissing,
    AuthFailed,
    RateLimited,
    UpstreamTimeout,
    UpstreamError,
    InvalidResponse,
    RequestCancelled
};

const char *statusCodeName(AIStatus status) noexcept;

struct AIMessage
{
    std::string role;
    std::string content;
};

struct TokenUsage
{
    std::int64_t promptTokens{0};
    std::int64_t completionTokens{0};
    std::int64_t totalTokens{0};
};

struct AIResult
{
    AIStatus status{AIStatus::UpstreamError};
    std::string content;
    std::string model;
    std::string finishReason;
    std::string errorMessage;
    std::string upstreamRequestId;
    TokenUsage usage;
    long httpStatus{0};
    long elapsedMs{0};
    bool truncated{false};

    bool ok() const noexcept { return status == AIStatus::Ok; }
};

} // namespace ai_service
} // namespace nexusim
