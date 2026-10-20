#include "ai_types.hpp"
#include "conversation_manager.hpp"
#include "deepseek_response.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace nexusim::ai_service;

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

void responseParsing()
{
    const std::string normal =
        R"({"id":"ds-1","model":"deepseek-v4-pro","choices":[{"message":{"role":"assistant","content":"你好\nNexusIM"},"finish_reason":"stop"}],"usage":{"prompt_tokens":8,"completion_tokens":4,"total_tokens":12}})";
    const AIResult result = DeepSeekResponseParser::parse(200, normal);
    require(result.ok(), "normal response should parse");
    require(result.content == "你好\nNexusIM", "UTF-8 content mismatch");
    require(result.upstreamRequestId == "ds-1", "response id fallback missing");
    require(result.usage.totalTokens == 12, "usage missing");

    require(DeepSeekResponseParser::parse(401, "{}").status ==
                AIStatus::AuthFailed,
            "401 mapping");
    require(DeepSeekResponseParser::parse(403, "{}").status ==
                AIStatus::AuthFailed,
            "403 mapping");
    require(DeepSeekResponseParser::parse(429, "{}").status ==
                AIStatus::RateLimited,
            "429 mapping");
    require(DeepSeekResponseParser::parse(500, "{}").status ==
                AIStatus::UpstreamError,
            "500 mapping");
    require(DeepSeekResponseParser::parse(200, "not-json").status ==
                AIStatus::InvalidResponse,
            "invalid JSON mapping");
    require(DeepSeekResponseParser::parse(200, R"({"model":"m","choices":[]})").status ==
                AIStatus::InvalidResponse,
            "empty choices mapping");
    require(DeepSeekResponseParser::parse(
                200,
                R"({"model":"m","choices":[{"message":{"content":" "},"finish_reason":"stop"}]})")
                .status == AIStatus::InvalidResponse,
            "empty content mapping");
    const AIResult truncated = DeepSeekResponseParser::parse(
        200,
        R"({"model":"m","choices":[{"message":{"content":"partial"},"finish_reason":"length"}]})",
        "header-id");
    require(truncated.ok() && truncated.truncated,
            "length finish reason should mark truncation");
    require(truncated.upstreamRequestId == "header-id",
            "header request id should win");

    const std::string longText(200000, 'x');
    const std::string longBody =
        std::string(R"({"model":"m","choices":[{"message":{"content":")") +
        longText + R"("},"finish_reason":"stop"}]})";
    const AIResult longResult = DeepSeekResponseParser::parse(200, longBody);
    require(longResult.ok() && longResult.content.size() == longText.size(),
            "long content should parse without truncation in the parser");
}

void conversationIsolationAndOrdering()
{
    ConversationManager manager(2, 4096, 8);
    {
        auto lease = manager.tryAcquire(1, "conversation-a");
        require(lease.has_value(), "first lease");
        require(!manager.tryAcquire(1, "conversation-a").has_value(),
                "same conversation must be single flight");
        auto first = manager.buildRequest(1, "conversation-a", "system", "one");
        require(first.ok && first.messages.size() == 2, "initial request shape");
        require(manager.commit(1, "conversation-a", "one", "answer-one"),
                "first commit");
    }
    {
        auto lease = manager.tryAcquire(1, "conversation-a");
        require(lease.has_value(), "second lease");
        auto second = manager.buildRequest(1, "conversation-a", "system", "two");
        require(second.ok && second.messages.size() == 4,
                "history should contain one complete turn");
        require(second.messages[1].content == "one" &&
                    second.messages[2].content == "answer-one",
                "history order mismatch");
        // No commit here: a failed upstream request must not fabricate history.
    }
    {
        auto lease = manager.tryAcquire(2, "conversation-a");
        require(lease.has_value(), "other user lease");
        auto isolated = manager.buildRequest(2, "conversation-a", "system", "fresh");
        require(isolated.ok && isolated.messages.size() == 2,
                "users sharing an opaque conversation id must remain isolated");
    }
    {
        auto lease = manager.tryAcquire(1, "conversation-a");
        auto afterFailure = manager.buildRequest(1, "conversation-a", "system", "three");
        require(lease.has_value() && afterFailure.messages.size() == 4,
                "failed request must not be committed");
    }
}

void historyAndCapacityBounds()
{
    ConversationManager manager(1, 4096, 2);
    for (const char *id : {"one", "two", "three"})
    {
        auto lease = manager.tryAcquire(9, id);
        require(lease.has_value(), "capacity lease");
        auto built = manager.buildRequest(9, id, "system", id);
        require(built.ok, "capacity request");
        require(manager.commit(9, id, id, std::string("reply-") + id),
                "capacity commit");
    }
    require(manager.conversationCount() == 2,
            "inactive LRU context should be evicted");

    auto lease = manager.tryAcquire(9, "three");
    auto built = manager.buildRequest(9, "three", "system", "next");
    require(lease.has_value() && built.messages.size() == 4,
            "configured history turn bound");
}
} // namespace

int main()
{
    responseParsing();
    conversationIsolationAndOrdering();
    historyAndCapacityBounds();
    std::cout << "stage5_ai_core_tests: PASS" << std::endl;
    return 0;
}
