#include "server/rpc/aiserviceclient.hpp"

#include "ai_service.pb.h"
#include "mprpcchannel.h"

namespace
{
nexusim::RpcCallStatus callStatus(const MprpcController &controller)
{
    if (controller.Failed())
        return {false, controller.ErrorCode(), controller.ErrorText()};
    return {true, mprpc::RpcErrorCode::Ok, ""};
}
} // namespace

namespace nexusim
{

const char *aiStatusName(int statusCode) noexcept
{
    switch (static_cast<ai::AIStatusCode>(statusCode))
    {
    case ai::AI_STATUS_OK: return "AI_OK";
    case ai::AI_STATUS_INVALID_ARGUMENT: return "AI_INVALID_ARGUMENT";
    case ai::AI_STATUS_BUSY: return "AI_SERVICE_BUSY";
    case ai::AI_STATUS_CONFIG_MISSING: return "AI_CONFIG_MISSING";
    case ai::AI_STATUS_AUTH_FAILED: return "AI_AUTH_FAILED";
    case ai::AI_STATUS_RATE_LIMITED: return "AI_RATE_LIMITED";
    case ai::AI_STATUS_UPSTREAM_TIMEOUT: return "AI_UPSTREAM_TIMEOUT";
    case ai::AI_STATUS_UPSTREAM_ERROR: return "AI_UPSTREAM_ERROR";
    case ai::AI_STATUS_INVALID_RESPONSE: return "AI_INVALID_RESPONSE";
    case ai::AI_STATUS_REQUEST_CANCELLED: return "AI_REQUEST_CANCELLED";
    }
    return "AI_UPSTREAM_ERROR";
}

AIChatRpcResult AIServiceClient::chat(
    int userId, const std::string &requestId,
    const std::string &conversationId, const std::string &prompt,
    int deadlineMs, Deadline deadline) const
{
    MprpcChannel channel;
    ai::AIService_Stub stub(&channel);
    ai::AIChatRequest request;
    request.set_user_id(userId);
    request.set_request_id(requestId);
    request.set_conversation_id(conversationId);
    request.set_prompt(prompt);
    request.set_deadline_ms(deadlineMs);
    ai::AIChatResponse response;
    MprpcController controller;
    controller.SetDeadline(deadline);
    stub.Chat(&controller, &request, &response, nullptr);

    AIChatRpcResult result;
    result.call = callStatus(controller);
    if (!result.call.ok) return result;
    if (response.request_id() != requestId ||
        response.conversation_id() != conversationId)
    {
        result.call = {false, mprpc::RpcErrorCode::ProtocolError,
                       "AIService returned mismatched request identity"};
        return result;
    }
    result.statusCode = static_cast<int>(response.status_code());
    result.content = response.content();
    result.model = response.model();
    result.errorMessage = response.error_message();
    result.finishReason = response.finish_reason();
    result.upstreamRequestId = response.upstream_request_id();
    result.httpStatus = response.http_status();
    if (response.has_usage())
    {
        result.promptTokens = response.usage().prompt_tokens();
        result.completionTokens = response.usage().completion_tokens();
        result.totalTokens = response.usage().total_tokens();
    }
    return result;
}

} // namespace nexusim
