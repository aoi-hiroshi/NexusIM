#pragma once

#include "ai_service.pb.h"
#include "conversation_manager.hpp"
#include "deepseek_client.hpp"

#include <atomic>
#include <cstddef>

namespace nexusim
{
namespace ai_service
{

class AIServiceImpl final : public ai::AIService
{
public:
    AIServiceImpl();

    void Chat(google::protobuf::RpcController *controller,
              const ai::AIChatRequest *request,
              ai::AIChatResponse *response,
              google::protobuf::Closure *done) override;

private:
    bool tryEnter();
    static ai::AIStatusCode protocolStatus(AIStatus status);

    DeepSeekClient client_;
    ConversationManager conversations_;
    const std::size_t maxPromptBytes_;
    const int maxConcurrent_;
    std::atomic<int> active_{0};
};

} // namespace ai_service
} // namespace nexusim
