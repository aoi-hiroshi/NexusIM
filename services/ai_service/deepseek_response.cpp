#include "deepseek_response.hpp"

#include "json.hpp"

#include <exception>

namespace
{
using json = nlohmann::json;

nexusim::ai_service::AIResult errorResult(
    nexusim::ai_service::AIStatus status, long httpStatus,
    const std::string &message, const std::string &requestId)
{
    nexusim::ai_service::AIResult result;
    result.status = status;
    result.httpStatus = httpStatus;
    result.errorMessage = message;
    result.upstreamRequestId = requestId;
    return result;
}

bool integerField(const json &object, const char *name, std::int64_t &value)
{
    const auto iterator = object.find(name);
    if (iterator == object.end()) return true;
    if (!iterator->is_number_integer()) return false;
    value = iterator->get<std::int64_t>();
    return value >= 0;
}
} // namespace

namespace nexusim
{
namespace ai_service
{

AIResult DeepSeekResponseParser::parse(long httpStatus, const std::string &body,
                                       const std::string &upstreamRequestId)
{
    if (httpStatus == 401 || httpStatus == 403)
        return errorResult(AIStatus::AuthFailed, httpStatus,
                           "DeepSeek rejected the API credentials",
                           upstreamRequestId);
    if (httpStatus == 429)
        return errorResult(AIStatus::RateLimited, httpStatus,
                           "DeepSeek rate limit was reached", upstreamRequestId);
    if (httpStatus < 200 || httpStatus >= 300)
        return errorResult(AIStatus::UpstreamError, httpStatus,
                           "DeepSeek returned HTTP " + std::to_string(httpStatus),
                           upstreamRequestId);
    if (body.empty())
        return errorResult(AIStatus::InvalidResponse, httpStatus,
                           "DeepSeek returned an empty response",
                           upstreamRequestId);

    try
    {
        const json document = json::parse(body);
        if (!document.is_object())
            return errorResult(AIStatus::InvalidResponse, httpStatus,
                               "DeepSeek response is not a JSON object",
                               upstreamRequestId);
        const auto choices = document.find("choices");
        if (choices == document.end() || !choices->is_array() || choices->empty() ||
            !choices->front().is_object())
            return errorResult(AIStatus::InvalidResponse, httpStatus,
                               "DeepSeek response has no choices",
                               upstreamRequestId);
        const json &choice = choices->front();
        const auto message = choice.find("message");
        const auto finishReason = choice.find("finish_reason");
        if (message == choice.end() || !message->is_object() ||
            finishReason == choice.end() || !finishReason->is_string())
            return errorResult(AIStatus::InvalidResponse, httpStatus,
                               "DeepSeek choice fields are invalid",
                               upstreamRequestId);
        const auto content = message->find("content");
        if (content == message->end() || !content->is_string() ||
            content->get<std::string>().find_first_not_of(" \t\r\n") ==
                std::string::npos)
            return errorResult(AIStatus::InvalidResponse, httpStatus,
                               "DeepSeek returned empty message content",
                               upstreamRequestId);
        const auto model = document.find("model");
        if (model == document.end() || !model->is_string() ||
            model->get<std::string>().empty())
            return errorResult(AIStatus::InvalidResponse, httpStatus,
                               "DeepSeek response model is missing",
                               upstreamRequestId);

        AIResult result;
        result.status = AIStatus::Ok;
        result.httpStatus = httpStatus;
        result.content = content->get<std::string>();
        result.model = model->get<std::string>();
        result.finishReason = finishReason->get<std::string>();
        result.truncated = result.finishReason == "length";
        result.upstreamRequestId = upstreamRequestId;
        const auto responseId = document.find("id");
        if (result.upstreamRequestId.empty() && responseId != document.end() &&
            responseId->is_string())
            result.upstreamRequestId = responseId->get<std::string>();

        const auto usage = document.find("usage");
        if (usage != document.end())
        {
            if (!usage->is_object() ||
                !integerField(*usage, "prompt_tokens", result.usage.promptTokens) ||
                !integerField(*usage, "completion_tokens",
                              result.usage.completionTokens) ||
                !integerField(*usage, "total_tokens", result.usage.totalTokens))
                return errorResult(AIStatus::InvalidResponse, httpStatus,
                                   "DeepSeek usage fields are invalid",
                                   result.upstreamRequestId);
        }
        return result;
    }
    catch (const std::exception &)
    {
        return errorResult(AIStatus::InvalidResponse, httpStatus,
                           "DeepSeek returned invalid JSON", upstreamRequestId);
    }
}

} // namespace ai_service
} // namespace nexusim
