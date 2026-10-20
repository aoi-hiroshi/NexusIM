#ifndef NEXUSIM_QT_SOCIALCONTROLLER_HPP
#define NEXUSIM_QT_SOCIALCONTROLLER_HPP

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QVector>

namespace nexusim {
namespace client {

class NetworkClient;
class RequestManager;
class SessionController;

struct GroupChatMessage
{
    int senderId{-1};
    QString senderName;
    QString text;
    QDateTime timestamp;
    bool outgoing{false};
    bool offline{false};
    bool system{false};
};

class SocialController : public QObject
{
    Q_OBJECT
public:
    SocialController(NetworkClient *network, RequestManager *requests,
                     SessionController *session, QObject *parent = nullptr);

    void startSession(const QJsonObject &loginResponse);
    void stopSession();
    const QVector<GroupChatMessage> &messages(int groupId) const;
    int unreadCount(int groupId) const;

public slots:
    void refreshFriends();
    void addFriend(int friendId);
    void refreshGroups();
    void createGroup(const QString &name, const QString &description);
    void joinGroup(int groupId);
    void leaveGroup(int groupId);
    void queryMembers(int groupId);
    void openGroup(int groupId);
    bool sendGroupMessage(int groupId, const QString &text);

signals:
    void friendsChanged(const QJsonArray &friends);
    void groupsChanged(const QJsonArray &groups);
    void groupMembersChanged(int groupId, const QJsonArray &members);
    void groupMessagesChanged(int groupId);
    void groupUnreadChanged(int groupId, int count);
    void operationSucceeded(const QString &message);
    void operationFailed(const QString &message);

private slots:
    void onMessage(const QJsonObject &message);
    void onRequestFinished(quint64 token, const QJsonObject &message);
    void onRequestFailed(quint64 token, const QString &reason,
                         const QJsonObject &message);

private:
    enum class Operation
    {
        AddFriend,
        QueryFriends,
        CreateGroup,
        JoinGroup,
        QueryGroups,
        QueryMembers,
        LeaveGroup,
        GroupChat
    };
    struct Pending
    {
        Operation operation{Operation::QueryFriends};
        int subjectId{-1};
    };

    QString nextRequestId();
    quint64 sendRequest(Operation operation, int requestType, int responseType,
                        QJsonObject request, int subjectId = -1,
                        int timeoutMs = 8000);
    void appendGroupMessage(int groupId, const QJsonObject &message, bool offline);

    NetworkClient *network_;
    RequestManager *requests_;
    SessionController *session_;
    QHash<quint64, Pending> pending_;
    QHash<int, QVector<GroupChatMessage>> messages_;
    QHash<int, int> unread_;
    quint64 sequence_{1};
    int activeGroupId_{-1};
};

} // namespace client
} // namespace nexusim

#endif
