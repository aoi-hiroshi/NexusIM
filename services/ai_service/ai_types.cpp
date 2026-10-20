#include "ai_types.hpp"

namespace nexusim
{
namespace ai_service
{

const char *statusCodeName(AIStatus status) noexcept
{
    switch (status)
    {
    case AIStatus::Ok: return "AI_OK";
    case AIStatus::InvalidArgument: return "AI_INVALID_ARGUMENT";
    case AIStatus::Busy: return "AI_SERVICE_BUSY";
    case AIStatus::ConfigMissing: return "AI_CONFIG_MISSING";
    case AIStatus::AuthFailed: return "AI_AUTH_FAILED";
    case AIStatus::RateLimited: return "AI_RATE_LIMITED";
    case AIStatus::UpstreamTimeout: return "AI_UPSTREAM_TIMEOUT";
    case AIStatus::UpstreamError: return "AI_UPSTREAM_ERROR";
    case AIStatus::InvalidResponse: return "AI_INVALID_RESPONSE";
    case AIStatus::RequestCancelled: return "AI_REQUEST_CANCELLED";
    }
    return "AI_UPSTREAM_ERROR";
}

} // namespace ai_service
} // namespace nexusim
