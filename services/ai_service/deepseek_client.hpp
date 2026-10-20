#pragma once

#include "ai_types.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace nexusim
{
namespace ai_service
{

struct DeepSeekConfig
{
    std::string apiKey;
    std::string baseUrl{"https://api.deepseek.com"};
    std::string model{"deepseek-v4-pro"};
    std::string thinking{"disabled"};
    std::string reasoningEffort{"none"};
    std::string systemPrompt{
        "You are the NexusIM DeepSeek assistant. Give accurate, concise and safe answers."};
    std::string caBundle;
    long connectTimeoutMs{5000};
    long totalTimeoutMs{75000};
    long maxTokens{2048};
    std::size_t maxResponseBytes{512U * 1024U};
    bool allowHttpMock{false};
    std::string validationError;

    static DeepSeekConfig fromEnvironment();
    bool valid() const noexcept { return validationError.empty(); }
};

class DeepSeekClient
{
public:
    explicit DeepSeekClient(DeepSeekConfig config =
                                DeepSeekConfig::fromEnvironment());

    const DeepSeekConfig &config() const noexcept { return config_; }
    AIResult chat(const std::vector<AIMessage> &messages,
                  long timeoutMs) const;

private:
    DeepSeekConfig config_;
    bool curlReady_{false};
};

} // namespace ai_service
} // namespace nexusim
