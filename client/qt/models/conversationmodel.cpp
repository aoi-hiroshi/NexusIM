#include "models/conversationmodel.hpp"

namespace nexusim {
namespace client {

ConversationModel::ConversationModel(QObject *parent) : QAbstractListModel(parent) {}

int ConversationModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : conversations_.size();
}

QVariant ConversationModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= conversations_.size())
        return QVariant();
    const Conversation &value = conversations_.at(index.row());
    switch (role)
    {
    case Qt::DisplayRole:
    case DisplayNameRole: return value.displayName;
    case UserIdRole: return value.userId;
    case LastMessageRole: return value.lastMessage;
    case TimestampRole: return value.lastTimestamp;
    case UnreadRole: return value.unreadCount;
    default: return QVariant();
    }
}

QHash<int, QByteArray> ConversationModel::roleNames() const
{
    return {{UserIdRole, "userId"},
            {DisplayNameRole, "displayName"},
            {LastMessageRole, "lastMessage"},
            {TimestampRole, "timestamp"},
            {UnreadRole, "unread"}};
}

int ConversationModel::ensureConversation(int userId, const QString &displayName)
{
    int row = indexOf(userId);
    if (row >= 0)
    {
        if (!displayName.isEmpty() && conversations_[row].displayName != displayName)
        {
            conversations_[row].displayName = displayName;
            emit dataChanged(index(row), index(row), {DisplayNameRole, Qt::DisplayRole});
        }
        return row;
    }
    beginInsertRows(QModelIndex(), conversations_.size(), conversations_.size());
    Conversation value;
    value.userId = userId;
    value.displayName = displayName.isEmpty()
                            ? QStringLiteral("用户 #%1").arg(userId)
                            : displayName;
    conversations_.push_back(value);
    row = conversations_.size() - 1;
    endInsertRows();
    return row;
}

bool ConversationModel::appendMessage(int userId, const QString &displayName,
                                      const ChatMessage &message,
                                      bool incrementUnread)
{
    const int row = ensureConversation(userId, displayName);
    Conversation &value = conversations_[row];
    value.messages.push_back(message);
    value.lastMessage = message.system
                            ? QStringLiteral("[系统] %1").arg(message.text)
                            : message.text.simplified();
    value.lastTimestamp = message.timestamp;
    if (incrementUnread && activeConversation_ != userId)
        ++value.unreadCount;
    emit dataChanged(index(row), index(row));
    emit conversationUpdated(userId);
    return true;
}

void ConversationModel::markRead(int userId)
{
    const int row = indexOf(userId);
    if (row < 0 || conversations_[row].unreadCount == 0) return;
    conversations_[row].unreadCount = 0;
    emit dataChanged(index(row), index(row), {UnreadRole});
}

void ConversationModel::setActiveConversation(int userId)
{
    activeConversation_ = userId;
    markRead(userId);
}

int ConversationModel::activeConversation() const { return activeConversation_; }

const Conversation *ConversationModel::conversation(int userId) const
{
    const int row = indexOf(userId);
    return row < 0 ? nullptr : &conversations_.at(row);
}

void ConversationModel::clear()
{
    beginResetModel();
    conversations_.clear();
    activeConversation_ = -1;
    endResetModel();
}

int ConversationModel::indexOf(int userId) const
{
    for (int i = 0; i < conversations_.size(); ++i)
        if (conversations_.at(i).userId == userId) return i;
    return -1;
}

} // namespace client
} // namespace nexusim
