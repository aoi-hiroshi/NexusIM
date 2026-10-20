#include "ai_service_impl.hpp"

#include "mprpccontroller.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{
std::size_t environmentSize(const char *name, std::size_t fallback,
                            std::size_t minimum, std::size_t maximum)
{
    const char *value = std::getenv(name);
    if (value == nullptr) return fallback;
    try
    {
        const auto parsed = static_cast<std::size_t>(std::stoull(value));
        return parsed < minimum || parsed > maximum ? fallback : parsed;
    }
    catch (...)
    {
        return fallback;
    }
}

class DoneGuard
{
public:
    explicit DoneGuard(google::protobuf::Closure *done) : done_(done) {}
    ~DoneGuard() { if (done_ != nullptr) done_->Run(); }
private:
    google::protobuf::Closure *done_;
};

class ActiveGuard
{
public:
    explicit ActiveGuard(std::atomic<int> *active) : active_(active) {}
    ~ActiveGuard() { if (active_ != nullptr) active_->fetch_sub(1); }
private:
    std::atomic<int> *active_;
};

bool validRequestId(const std::string &value)
{
    if (value.empty() || value.size() > 128) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '-' ||
               character == '_' || character == '.' || character == ':';
    });
}

long effectiveTimeout(const nexusim::ai::AIChatRequest &request,
                      google::protobuf::RpcController *controller)
{
    long timeout = request.deadline_ms();
    if (timeout < 1000 || timeout > 120000) timeout = 85000;
    if (auto *mprpc = dynamic_cast<MprpcController *>(controller))
    {
        const auto now = MprpcController::Clock::now();
        if (now >= mprpc->Deadline()) return 0;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            mprpc->Deadline() - now).count();
        timeout = std::min<long>(timeout, static_cast<long>(remaining));
    }
    return timeout > 500 ? timeout - 250 : 0;
}
} // namespace

namespace nexusim
{
namespace ai_service
{

AIServiceImpl::AIServiceImpl()
    : conversations_(environmentSize("AI_HISTORY_TURNS", 10, 1, 50),
                     environmentSize("AI_MAX_CONTEXT_BYTES", 65536, 1024,
                                     1024U * 1024U),
                     environmentSize("AI_MAX_CONVERSATIONS", 1000, 1, 100000)),
      maxPromptBytes_(environmentSize("AI_MAX_PROMPT_BYTES", 32768, 1,
                                      1024U * 1024U)),
      maxConcurrent_(static_cast<int>(environmentSize(
          "AI_MAX_CONCURRENT_REQUESTS", 4, 1, 128))) {}

bool AIServiceImpl::tryEnter()
{
    int current = active_.load();
    while (current < maxConcurrent_)
    {
        if (active_.compare_exchange_weak(current, current + 1)) return true;
    }
    return false;
}

ai::AIStatusCode AIServiceImpl::protocolStatus(AIStatus status)
{
    switch (status)
    {
    case AIStatus::Ok: return ai::AI_STATUS_OK;
    case AIStatus::InvalidArgument: return ai::AI_STATUS_INVALID_ARGUMENT;
    case AIStatus::Busy: return ai::AI_STATUS_BUSY;
    case AIStatus::ConfigMissing: return ai::AI_STATUS_CONFIG_MISSING;
    case AIStatus::AuthFailed: return ai::AI_STATUS_AUTH_FAILED;
    case AIStatus::RateLimited: return ai::AI_STATUS_RATE_LIMITED;
    case AIStatus::UpstreamTimeout: return ai::AI_STATUS_UPSTREAM_TIMEOUT;
    case AIStatus::UpstreamError: return ai::AI_STATUS_UPSTREAM_ERROR;
    case AIStatus::InvalidResponse: return ai::AI_STATUS_INVALID_RESPONSE;
    case AIStatus::RequestCancelled: return ai::AI_STATUS_REQUEST_CANCELLED;
    }
    return ai::AI_STATUS_UPSTREAM_ERROR;
}

void AIServiceImpl::Chat(google::protobuf::RpcController *controller,
                         const ai::AIChatRequest *request,
                         ai::AIChatResponse *response,
                         google::protobuf::Closure *done)
{
    DoneGuard doneGuard(done);
    if (response == nullptr)
    {
        if (controller != nullptr) controller->SetFailed("AI response is null");
        return;
    }
    if (request != nullptr)
    {
        response->set_request_id(request->request_id());
        response->set_conversation_id(request->conversation_id());
    }
    response->set_model(client_.config().model);
    if (request == nullptr || !validRequestId(request->request_id()) ||
        request->user_id() <= 0 || !validRequestId(request->conversation_id()) ||
        request->prompt().empty() ||
        request->prompt().find_first_not_of(" \t\r\n") == std::string::npos ||
        request->prompt().size() > maxPromptBytes_)
    {
        response->set_status_code(ai::AI_STATUS_INVALID_ARGUMENT);
        response->set_error_message("invalid AI request");
        return;
    }
    if ((controller != nullptr && controller->IsCanceled()) ||
        effectiveTimeout(*request, controller) <= 0)
    {
        response->set_status_code(ai::AI_STATUS_REQUEST_CANCELLED);
        response->set_error_message("AI request deadline expired before execution");
        return;
    }
    if (!tryEnter())
    {
        response->set_status_code(ai::AI_STATUS_BUSY);
        response->set_error_message("AIService concurrency limit reached");
        return;
    }
    ActiveGuard activeGuard(&active_);
    auto lease = conversations_.tryAcquire(request->user_id(),
                                           request->conversation_id());
    if (!lease)
    {
        response->set_status_code(ai::AI_STATUS_BUSY);
        response->set_error_message("this AI conversation already has a pending request");
        return;
    }
    auto prepared = conversations_.buildRequest(
        request->user_id(), request->conversation_id(),
        client_.config().systemPrompt, request->prompt());
    if (!prepared.ok)
    {
        response->set_status_code(protocolStatus(prepared.status));
        response->set_error_message(prepared.error);
        return;
    }

    const long timeout = effectiveTimeout(*request, controller);
    if (timeout <= 0)
    {
        response->set_status_code(ai::AI_STATUS_REQUEST_CANCELLED);
        response->set_error_message("AI request deadline expired before HTTPS call");
        return;
    }
    AIResult result = client_.chat(prepared.messages, timeout);
    if ((controller != nullptr && controller->IsCanceled()) ||
        effectiveTimeout(*request, controller) <= 0)
    {
        result.status = AIStatus::RequestCancelled;
        result.content.clear();
        result.errorMessage = "AI request completed after cancellation or deadline";
    }
    if (result.ok() && !conversations_.commit(
                           request->user_id(), request->conversation_id(),
                           request->prompt(), result.content))
    {
        result.status = AIStatus::RequestCancelled;
        result.content.clear();
        result.errorMessage = "AI conversation changed before the reply was committed";
    }

    response->set_status_code(protocolStatus(result.status));
    response->set_content(result.content);
    if (!result.model.empty()) response->set_model(result.model);
    response->set_finish_reason(result.finishReason);
    response->set_http_status(static_cast<int>(result.httpStatus));
    response->set_upstream_request_id(result.upstreamRequestId);
    response->mutable_usage()->set_prompt_tokens(result.usage.promptTokens);
    response->mutable_usage()->set_completion_tokens(result.usage.completionTokens);
    response->mutable_usage()->set_total_tokens(result.usage.totalTokens);
    if (result.truncated)
        response->set_error_message(
            "response was truncated by the model token or context limit");
    else
        response->set_error_message(result.errorMessage);

    std::clog << "AI request_id=" << request->request_id()
              << " user_id=" << request->user_id()
              << " status=" << statusCodeName(result.status)
              << " rpc_http_ms=" << result.elapsedMs
              << " http_status=" << result.httpStatus
              << " total_tokens=" << result.usage.totalTokens << std::endl;
}

} // namespace ai_service
} // namespace nexusim
