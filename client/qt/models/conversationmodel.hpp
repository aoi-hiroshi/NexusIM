#ifndef NEXUSIM_QT_CONVERSATIONMODEL_HPP
#define NEXUSIM_QT_CONVERSATIONMODEL_HPP

#include <QAbstractListModel>
#include <QDateTime>
#include <QVector>

namespace nexusim {
namespace client {

struct ChatMessage
{
    QString text;
    QString senderName;
    QDateTime timestamp;
    bool outgoing{false};
    bool system{false};
    bool offline{false};
};

struct Conversation
{
    int userId{-1};
    QString displayName;
    QString lastMessage;
    QDateTime lastTimestamp;
    int unreadCount{0};
    QVector<ChatMessage> messages;
};

class ConversationModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role
    {
        UserIdRole = Qt::UserRole + 1,
        DisplayNameRole,
        LastMessageRole,
        TimestampRole,
        UnreadRole
    };

    explicit ConversationModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int ensureConversation(int userId, const QString &displayName = QString());
    bool appendMessage(int userId, const QString &displayName,
                       const ChatMessage &message, bool incrementUnread);
    void markRead(int userId);
    void setActiveConversation(int userId);
    int activeConversation() const;
    const Conversation *conversation(int userId) const;
    void clear();

signals:
    void conversationUpdated(int userId);

private:
    int indexOf(int userId) const;

    QVector<Conversation> conversations_;
    int activeConversation_{-1};
};

} // namespace client
} // namespace nexusim

#endif
