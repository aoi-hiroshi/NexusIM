#pragma once

#include "ai_types.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nexusim
{
namespace ai_service
{

class ConversationManager
{
public:
    struct BuildResult
    {
        bool ok{false};
        AIStatus status{AIStatus::InvalidArgument};
        std::string error;
        std::vector<AIMessage> messages;
    };

    class Lease
    {
    public:
        Lease() = default;
        Lease(Lease &&other) noexcept;
        Lease &operator=(Lease &&other) noexcept;
        ~Lease();

        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        explicit operator bool() const noexcept { return owner_ != nullptr; }

    private:
        friend class ConversationManager;
        struct KeyData
        {
            int userId{-1};
            std::string conversationId;
        };
        Lease(ConversationManager *owner, KeyData key);
        void reset() noexcept;

        ConversationManager *owner_{nullptr};
        KeyData key_;
    };

    ConversationManager(std::size_t maxTurns, std::size_t maxContextBytes,
                        std::size_t maxConversations);

    std::optional<Lease> tryAcquire(int userId,
                                    const std::string &conversationId);
    BuildResult buildRequest(int userId, const std::string &conversationId,
                             const std::string &systemPrompt,
                             const std::string &prompt);
    bool commit(int userId, const std::string &conversationId,
                const std::string &prompt, const std::string &assistantReply);
    bool clear(int userId, const std::string &conversationId);
    std::size_t conversationCount() const;

private:
    struct Key
    {
        int userId{-1};
        std::string conversationId;
        bool operator==(const Key &other) const noexcept
        {
            return userId == other.userId &&
                   conversationId == other.conversationId;
        }
    };
    struct KeyHash
    {
        std::size_t operator()(const Key &key) const noexcept;
    };
    struct Context
    {
        std::vector<AIMessage> history;
        std::uint64_t lastUsed{0};
    };

    static bool validConversationId(const std::string &value);
    void release(int userId, const std::string &conversationId) noexcept;
    bool ensureCapacityLocked(const Key &requested);
    void trimStoredHistory(Context &context);

    const std::size_t maxTurns_;
    const std::size_t maxContextBytes_;
    const std::size_t maxConversations_;
    mutable std::mutex mutex_;
    std::unordered_map<Key, Context, KeyHash> contexts_;
    std::unordered_set<Key, KeyHash> active_;
    std::uint64_t sequence_{1};
};

} // namespace ai_service
} // namespace nexusim
