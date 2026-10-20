#include "deepseek_client.hpp"

#include "deepseek_response.hpp"
#include "json.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace
{
using json = nlohmann::json;

std::string environmentOr(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    return value == nullptr || *value == '\0' ? std::string(fallback)
                                               : std::string(value);
}

long environmentLong(const char *name, long fallback, long minimum,
                     long maximum)
{
    try
    {
        const long value = std::stol(environmentOr(name, ""));
        return value < minimum || value > maximum ? fallback : value;
    }
    catch (...)
    {
        return fallback;
    }
}

bool environmentBool(const char *name, bool fallback)
{
    std::string value = environmentOr(name, fallback ? "1" : "0");
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    if (value == "1" || value == "true" || value == "yes") return true;
    if (value == "0" || value == "false" || value == "no") return false;
    return fallback;
}

bool startsWith(const std::string &value, const std::string &prefix)
{
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

bool localHttpUrl(const std::string &url)
{
    return startsWith(url, "http://127.0.0.1:") ||
           startsWith(url, "http://localhost:") ||
           startsWith(url, "http://[::1]:");
}

struct ResponseBuffer
{
    std::string body;
    std::size_t limit{0};
    bool overflow{false};
};

struct ResponseHeaders
{
    std::string requestId;
};

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

std::size_t writeBody(char *data, std::size_t size, std::size_t count,
                      void *context)
{
    auto *buffer = static_cast<ResponseBuffer *>(context);
    if (buffer == nullptr || (size != 0 && count >
            std::numeric_limits<std::size_t>::max() / size))
        return 0;
    const std::size_t bytes = size * count;
    if (bytes > buffer->limit - std::min(buffer->body.size(), buffer->limit))
    {
        buffer->overflow = true;
        return 0;
    }
    buffer->body.append(data, bytes);
    return bytes;
}

std::size_t readHeader(char *data, std::size_t size, std::size_t count,
                       void *context)
{
    auto *headers = static_cast<ResponseHeaders *>(context);
    if (headers == nullptr || (size != 0 && count >
            std::numeric_limits<std::size_t>::max() / size))
        return 0;
    const std::size_t bytes = size * count;
    const std::string line(data, bytes);
    const auto separator = line.find(':');
    if (separator != std::string::npos)
    {
        const std::string name = lower(trim(line.substr(0, separator)));
        if (name == "x-request-id" || name == "x-ds-request-id" ||
            name == "request-id")
            headers->requestId = trim(line.substr(separator + 1));
    }
    return bytes;
}

struct CurlDeleter
{
    void operator()(CURL *handle) const noexcept
    {
        if (handle != nullptr) curl_easy_cleanup(handle);
    }
};

struct HeaderDeleter
{
    void operator()(curl_slist *headers) const noexcept
    {
        if (headers != nullptr) curl_slist_free_all(headers);
    }
};

bool initializeCurl()
{
    static std::once_flag once;
    static bool ready = false;
    std::call_once(once, [] { ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK; });
    return ready;
}

nexusim::ai_service::AIResult clientError(
    nexusim::ai_service::AIStatus status, const std::string &message,
    long elapsedMs = 0)
{
    nexusim::ai_service::AIResult result;
    result.status = status;
    result.errorMessage = message;
    result.elapsedMs = elapsedMs;
    return result;
}
} // namespace

namespace nexusim
{
namespace ai_service
{

DeepSeekConfig DeepSeekConfig::fromEnvironment()
{
    DeepSeekConfig config;
    config.apiKey = environmentOr("DEEPSEEK_API_KEY", "");
    config.baseUrl = environmentOr("DEEPSEEK_BASE_URL", config.baseUrl.c_str());
    while (!config.baseUrl.empty() && config.baseUrl.back() == '/')
        config.baseUrl.pop_back();
    config.model = environmentOr("DEEPSEEK_MODEL", config.model.c_str());
    config.thinking = environmentOr("DEEPSEEK_THINKING", config.thinking.c_str());
    config.reasoningEffort = environmentOr(
        "DEEPSEEK_REASONING_EFFORT",
        config.thinking == "enabled" ? "high" : "none");
    config.systemPrompt = environmentOr("DEEPSEEK_SYSTEM_PROMPT",
                                        config.systemPrompt.c_str());
    config.caBundle = environmentOr("DEEPSEEK_CA_BUNDLE", "");
    config.connectTimeoutMs = environmentLong(
        "DEEPSEEK_HTTP_CONNECT_TIMEOUT_MS", config.connectTimeoutMs, 100, 30000);
    config.totalTimeoutMs = environmentLong(
        "DEEPSEEK_HTTP_TIMEOUT_MS", config.totalTimeoutMs, 1000, 300000);
    config.maxTokens = environmentLong("DEEPSEEK_MAX_TOKENS",
                                       config.maxTokens, 1, 393216);
    config.maxResponseBytes = static_cast<std::size_t>(environmentLong(
        "DEEPSEEK_MAX_RESPONSE_BYTES", static_cast<long>(config.maxResponseBytes),
        1024, 768L * 1024L));
    config.allowHttpMock = environmentBool("DEEPSEEK_ALLOW_HTTP_MOCK", false);

    if (config.apiKey.empty())
        config.validationError = "DEEPSEEK_API_KEY is not configured";
    else if (config.model.empty() || config.model.size() > 128)
        config.validationError = "DEEPSEEK_MODEL is invalid";
    else if (config.systemPrompt.size() > 8192)
        config.validationError = "DEEPSEEK_SYSTEM_PROMPT is too large";
    else if (config.thinking != "enabled" && config.thinking != "disabled")
        config.validationError = "DEEPSEEK_THINKING must be enabled or disabled";
    else if (config.reasoningEffort != "none" &&
             config.reasoningEffort != "low" &&
             config.reasoningEffort != "high" &&
             config.reasoningEffort != "max")
        config.validationError =
            "DEEPSEEK_REASONING_EFFORT must be none, low, high, or max";
    else if (config.thinking == "enabled" && config.reasoningEffort == "none")
        config.validationError =
            "enabled thinking requires low, high, or max reasoning effort";
    else if (config.thinking == "disabled" && config.reasoningEffort != "none")
        config.validationError =
            "disabled thinking requires reasoning effort none";
    else if (!startsWith(config.baseUrl, "https://") &&
             !(config.allowHttpMock && localHttpUrl(config.baseUrl)))
        config.validationError =
            "DEEPSEEK_BASE_URL must use HTTPS; HTTP is only allowed for an explicit loopback mock";
    return config;
}

DeepSeekClient::DeepSeekClient(DeepSeekConfig config)
    : config_(std::move(config)), curlReady_(initializeCurl())
{
    if (!curlReady_ && config_.validationError.empty())
        config_.validationError = "libcurl global initialization failed";
}

AIResult DeepSeekClient::chat(const std::vector<AIMessage> &messages,
                              long timeoutMs) const
{
    if (!config_.valid())
        return clientError(AIStatus::ConfigMissing, config_.validationError);
    if (!curlReady_)
        return clientError(AIStatus::UpstreamError,
                           "libcurl is unavailable");
    if (messages.empty())
        return clientError(AIStatus::InvalidArgument,
                           "AI messages cannot be empty");
    const long effectiveTimeout = std::max<long>(1, std::min(
        config_.totalTimeoutMs, timeoutMs));

    json request;
    request["model"] = config_.model;
    request["stream"] = false;
    request["max_tokens"] = config_.maxTokens;
    request["thinking"] = {{"type", config_.thinking}};
    request["reasoning_effort"] = config_.reasoningEffort;
    request["messages"] = json::array();
    for (const auto &message : messages)
        request["messages"].push_back(
            {{"role", message.role}, {"content", message.content}});
    const std::string requestBody = request.dump();

    std::unique_ptr<CURL, CurlDeleter> handle(curl_easy_init());
    if (!handle)
        return clientError(AIStatus::UpstreamError,
                           "could not create a libcurl request");
    std::unique_ptr<curl_slist, HeaderDeleter> headers(
        curl_slist_append(nullptr, "Content-Type: application/json"));
    if (!headers)
        return clientError(AIStatus::UpstreamError,
                           "could not allocate HTTP headers");
    const std::string authorization = "Authorization: Bearer " + config_.apiKey;
    curl_slist *updatedHeaders = curl_slist_append(headers.get(),
                                                   authorization.c_str());
    if (updatedHeaders == nullptr)
        return clientError(AIStatus::UpstreamError,
                           "could not allocate HTTP headers");
    headers.release();
    headers.reset(updatedHeaders);

    ResponseBuffer response{{}, config_.maxResponseBytes, false};
    ResponseHeaders responseHeaders;
    const std::string endpoint = config_.baseUrl + "/chat/completions";
    curl_easy_setopt(handle.get(), CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(handle.get(), CURLOPT_POST, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, requestBody.data());
    curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(requestBody.size()));
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT_MS,
                     std::min(config_.connectTimeoutMs, effectiveTimeout));
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS, effectiveTimeout);
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &writeBody);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION, &readHeader);
    curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, &responseHeaders);
    curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "NexusIM-AIService/1.0");
    if (!config_.caBundle.empty())
        curl_easy_setopt(handle.get(), CURLOPT_CAINFO, config_.caBundle.c_str());

    const auto started = std::chrono::steady_clock::now();
    const CURLcode curlResult = curl_easy_perform(handle.get());
    const long elapsed = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count());
    long httpStatus = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &httpStatus);
    if (curlResult != CURLE_OK)
    {
        if (curlResult == CURLE_OPERATION_TIMEDOUT)
            return clientError(AIStatus::UpstreamTimeout,
                               "DeepSeek request timed out", elapsed);
        if (response.overflow)
            return clientError(AIStatus::InvalidResponse,
                               "DeepSeek response exceeded the configured size limit",
                               elapsed);
        return clientError(AIStatus::UpstreamError,
                           std::string("DeepSeek HTTPS request failed: ") +
                               curl_easy_strerror(curlResult),
                           elapsed);
    }

    AIResult result = DeepSeekResponseParser::parse(
        httpStatus, response.body, responseHeaders.requestId);
    result.elapsedMs = elapsed;
    return result;
}

} // namespace ai_service
} // namespace nexusim
