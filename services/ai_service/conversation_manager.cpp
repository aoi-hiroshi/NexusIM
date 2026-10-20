#include "conversation_manager.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>

namespace nexusim
{
namespace ai_service
{

ConversationManager::Lease::Lease(ConversationManager *owner, KeyData key)
    : owner_(owner), key_(std::move(key)) {}

ConversationManager::Lease::Lease(Lease &&other) noexcept
    : owner_(other.owner_), key_(std::move(other.key_))
{
    other.owner_ = nullptr;
}

ConversationManager::Lease &ConversationManager::Lease::operator=(
    Lease &&other) noexcept
{
    if (this == &other) return *this;
    reset();
    owner_ = other.owner_;
    key_ = std::move(other.key_);
    other.owner_ = nullptr;
    return *this;
}

ConversationManager::Lease::~Lease() { reset(); }

void ConversationManager::Lease::reset() noexcept
{
    if (owner_ != nullptr)
        owner_->release(key_.userId, key_.conversationId);
    owner_ = nullptr;
}

ConversationManager::ConversationManager(std::size_t maxTurns,
                                         std::size_t maxContextBytes,
                                         std::size_t maxConversations)
    : maxTurns_(std::max<std::size_t>(1, maxTurns)),
      maxContextBytes_(std::max<std::size_t>(1024, maxContextBytes)),
      maxConversations_(std::max<std::size_t>(1, maxConversations)) {}

std::size_t ConversationManager::KeyHash::operator()(const Key &key) const noexcept
{
    const std::size_t first = std::hash<int>{}(key.userId);
    const std::size_t second = std::hash<std::string>{}(key.conversationId);
    return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
}

bool ConversationManager::validConversationId(const std::string &value)
{
    if (value.empty() || value.size() > 128) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == '-' ||
               character == '_' || character == '.' || character == ':';
    });
}

std::optional<ConversationManager::Lease> ConversationManager::tryAcquire(
    int userId, const std::string &conversationId)
{
    if (userId <= 0 || !validConversationId(conversationId)) return std::nullopt;
    const Key key{userId, conversationId};
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_.find(key) != active_.end()) return std::nullopt;
    active_.insert(key);
    return Lease(this, Lease::KeyData{userId, conversationId});
}

ConversationManager::BuildResult ConversationManager::buildRequest(
    int userId, const std::string &conversationId,
    const std::string &systemPrompt, const std::string &prompt)
{
    BuildResult result;
    if (userId <= 0 || !validConversationId(conversationId) || prompt.empty())
    {
        result.error = "invalid AI conversation or prompt";
        return result;
    }
    const Key key{userId, conversationId};
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_.find(key) == active_.end())
    {
        result.status = AIStatus::RequestCancelled;
        result.error = "conversation lease is not active";
        return result;
    }
    if (!ensureCapacityLocked(key))
    {
        result.status = AIStatus::Busy;
        result.error = "AI conversation capacity is full";
        return result;
    }

    Context &context = contexts_[key];
    context.lastUsed = sequence_++;
    const std::size_t fixedBytes = systemPrompt.size() + prompt.size();
    if (fixedBytes > maxContextBytes_)
    {
        result.error = "prompt exceeds the configured context limit";
        return result;
    }

    if (!systemPrompt.empty())
        result.messages.push_back({"system", systemPrompt});
    std::vector<AIMessage> selected;
    std::size_t usedBytes = fixedBytes;
    const auto &history = context.history;
    std::size_t turns = 0;
    for (std::size_t end = history.size(); end >= 2 && turns < maxTurns_; end -= 2)
    {
        const AIMessage &user = history[end - 2];
        const AIMessage &assistant = history[end - 1];
        const std::size_t pairBytes = user.content.size() + assistant.content.size();
        if (pairBytes > maxContextBytes_ - usedBytes) break;
        selected.insert(selected.begin(), assistant);
        selected.insert(selected.begin(), user);
        usedBytes += pairBytes;
        ++turns;
    }
    result.messages.insert(result.messages.end(), selected.begin(), selected.end());
    result.messages.push_back({"user", prompt});
    result.ok = true;
    result.status = AIStatus::Ok;
    return result;
}

bool ConversationManager::commit(int userId,
                                 const std::string &conversationId,
                                 const std::string &prompt,
                                 const std::string &assistantReply)
{
    if (prompt.empty() || assistantReply.empty()) return false;
    const Key key{userId, conversationId};
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_.find(key) == active_.end()) return false;
    auto iterator = contexts_.find(key);
    if (iterator == contexts_.end()) return false;
    iterator->second.history.push_back({"user", prompt});
    iterator->second.history.push_back({"assistant", assistantReply});
    iterator->second.lastUsed = sequence_++;
    trimStoredHistory(iterator->second);
    return true;
}

bool ConversationManager::clear(int userId, const std::string &conversationId)
{
    const Key key{userId, conversationId};
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_.find(key) != active_.end()) return false;
    return contexts_.erase(key) != 0;
}

std::size_t ConversationManager::conversationCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return contexts_.size();
}

void ConversationManager::release(int userId,
                                  const std::string &conversationId) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    active_.erase(Key{userId, conversationId});
}

bool ConversationManager::ensureCapacityLocked(const Key &requested)
{
    if (contexts_.find(requested) != contexts_.end()) return true;
    if (contexts_.size() < maxConversations_) return true;

    auto victim = contexts_.end();
    std::uint64_t oldest = std::numeric_limits<std::uint64_t>::max();
    for (auto iterator = contexts_.begin(); iterator != contexts_.end(); ++iterator)
    {
        if (active_.find(iterator->first) != active_.end()) continue;
        if (iterator->second.lastUsed < oldest)
        {
            oldest = iterator->second.lastUsed;
            victim = iterator;
        }
    }
    if (victim == contexts_.end()) return false;
    contexts_.erase(victim);
    return true;
}

void ConversationManager::trimStoredHistory(Context &context)
{
    const std::size_t maxMessages = maxTurns_ * 2;
    auto bytes = [&context] {
        std::size_t total = 0;
        for (const auto &message : context.history) total += message.content.size();
        return total;
    }();
    while (context.history.size() > maxMessages ||
           (bytes > maxContextBytes_ && context.history.size() >= 2))
    {
        bytes -= context.history[0].content.size();
        bytes -= context.history[1].content.size();
        context.history.erase(context.history.begin(), context.history.begin() + 2);
    }
}

} // namespace ai_service
} // namespace nexusim
