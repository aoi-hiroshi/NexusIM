#include "controllers/socialcontroller.hpp"

#include "controllers/sessioncontroller.hpp"
#include "network/networkclient.hpp"
#include "network/requestmanager.hpp"
#include "protocol/messageprotocol.hpp"

#include <QJsonDocument>

#include <utility>

namespace nexusim {
namespace client {

SocialController::SocialController(NetworkClient *network, RequestManager *requests,
                                   SessionController *session, QObject *parent)
    : QObject(parent), network_(network), requests_(requests), session_(session)
{
    connect(network_, &NetworkClient::messageReceived,
            this, &SocialController::onMessage);
    connect(requests_, &RequestManager::requestFinished,
            this, &SocialController::onRequestFinished);
    connect(requests_, &RequestManager::requestFailed,
            this, &SocialController::onRequestFailed);
}

void SocialController::startSession(const QJsonObject &loginResponse)
{
    pending_.clear();
    messages_.clear();
    unread_.clear();
    activeGroupId_ = -1;
    QJsonArray initialFriends;
    for (const QJsonObject &entry : protocol::decodeObjectStrings(
             loginResponse.value(QStringLiteral("friends"))))
        initialFriends.push_back(entry);
    emit friendsChanged(initialFriends);

    for (const QJsonObject &entry : protocol::decodeObjectStrings(
             loginResponse.value(QStringLiteral("offlinemsg"))))
    {
        if (entry.value(QStringLiteral("msgid")).toInt(-1) == protocol::GroupChat)
            appendGroupMessage(entry.value(QStringLiteral("groupid")).toInt(-1),
                               entry, true);
    }
    refreshFriends();
    refreshGroups();
}

void SocialController::stopSession()
{
    pending_.clear();
    messages_.clear();
    unread_.clear();
    activeGroupId_ = -1;
    emit friendsChanged(QJsonArray());
    emit groupsChanged(QJsonArray());
}

const QVector<GroupChatMessage> &SocialController::messages(int groupId) const
{
    static const QVector<GroupChatMessage> empty;
    const auto it = messages_.constFind(groupId);
    return it == messages_.constEnd() ? empty : it.value();
}

int SocialController::unreadCount(int groupId) const
{
    return unread_.value(groupId);
}

QString SocialController::nextRequestId()
{
    return QStringLiteral("qt-s6-%1-%2")
        .arg(session_->session().userId).arg(sequence_++);
}

quint64 SocialController::sendRequest(Operation operation, int requestType,
                                      int responseType, QJsonObject request,
                                      int subjectId, int timeoutMs)
{
    if (!session_->isAuthenticated() || !network_->isConnected())
    {
        emit operationFailed(QStringLiteral("当前会话未连接"));
        return 0;
    }
    const QString requestId = request.value(QStringLiteral("request_id")).toString();
    const quint64 token = requests_->startRequest(
        requestType, responseType, timeoutMs, requestId);
    if (!token)
    {
        emit operationFailed(QStringLiteral("同类请求正在处理中"));
        return 0;
    }
    pending_.insert(token, {operation, subjectId});
    QString error;
    if (!network_->sendJson(request, &error))
    {
        requests_->cancelRequest(token, error);
        return 0;
    }
    return token;
}

void SocialController::refreshFriends()
{
    const QString id = nextRequestId();
    sendRequest(Operation::QueryFriends, protocol::QueryFriends,
                protocol::QueryFriendsAck, protocol::queryFriendsRequest(id));
}

void SocialController::addFriend(int friendId)
{
    const QString id = nextRequestId();
    sendRequest(Operation::AddFriend, protocol::AddFriend, protocol::AddFriendAck,
                protocol::addFriendRequest(id, friendId), friendId);
}

void SocialController::refreshGroups()
{
    const QString id = nextRequestId();
    sendRequest(Operation::QueryGroups, protocol::QueryGroups,
                protocol::QueryGroupsAck, protocol::queryGroupsRequest(id));
}

void SocialController::createGroup(const QString &name, const QString &description)
{
    const QString normalized = name.trimmed();
    if (normalized.isEmpty() || normalized.toUtf8().size() > 50 ||
        description.toUtf8().size() > 200)
    {
        emit operationFailed(QStringLiteral("群名称需为 1–50 字节，描述不能超过 200 字节"));
        return;
    }
    const QString id = nextRequestId();
    sendRequest(Operation::CreateGroup, protocol::CreateGroup,
                protocol::CreateGroupAck,
                protocol::createGroupRequest(id, normalized, description));
}

void SocialController::joinGroup(int groupId)
{
    const QString id = nextRequestId();
    sendRequest(Operation::JoinGroup, protocol::JoinGroup, protocol::JoinGroupAck,
                protocol::joinGroupRequest(id, groupId), groupId);
}

void SocialController::leaveGroup(int groupId)
{
    const QString id = nextRequestId();
    sendRequest(Operation::LeaveGroup, protocol::LeaveGroup, protocol::LeaveGroupAck,
                protocol::leaveGroupRequest(id, groupId), groupId);
}

void SocialController::queryMembers(int groupId)
{
    const QString id = nextRequestId();
    sendRequest(Operation::QueryMembers, protocol::QueryGroupMembers,
                protocol::QueryGroupMembersAck,
                protocol::queryGroupMembersRequest(id, groupId), groupId);
}

void SocialController::openGroup(int groupId)
{
    activeGroupId_ = groupId;
    unread_[groupId] = 0;
    emit groupUnreadChanged(groupId, 0);
    queryMembers(groupId);
    emit groupMessagesChanged(groupId);
}

bool SocialController::sendGroupMessage(int groupId, const QString &text)
{
    const QString normalized = text.trimmed();
    if (groupId <= 0 || normalized.isEmpty() || normalized.toUtf8().size() > 65536)
    {
        emit operationFailed(QStringLiteral("群消息不能为空且不能超过 65536 个 UTF-8 字节"));
        return false;
    }
    const QString requestId = nextRequestId();
    const QDateTime now = QDateTime::currentDateTime();
    if (!sendRequest(Operation::GroupChat, protocol::GroupChat,
                     protocol::GroupChatAck,
                     protocol::groupChatRequest(
                         requestId, groupId, normalized,
                         now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))),
                     groupId, 15000))
        return false;
    GroupChatMessage message;
    message.senderId = session_->session().userId;
    message.senderName = session_->session().userName;
    message.text = normalized;
    message.timestamp = now;
    message.outgoing = true;
    messages_[groupId].push_back(std::move(message));
    emit groupMessagesChanged(groupId);
    return true;
}

void SocialController::onMessage(const QJsonObject &message)
{
    if (message.value(QStringLiteral("msgid")).toInt(-1) != protocol::GroupChat)
        return;
    appendGroupMessage(message.value(QStringLiteral("groupid")).toInt(-1),
                       message, false);
}

void SocialController::appendGroupMessage(int groupId, const QJsonObject &message,
                                          bool offline)
{
    const int senderId = message.value(QStringLiteral("id")).toInt(-1);
    const QString text = message.value(QStringLiteral("msg")).toString();
    if (groupId <= 0 || senderId <= 0 || text.isEmpty()) return;
    GroupChatMessage value;
    value.senderId = senderId;
    value.senderName = message.value(QStringLiteral("name")).toString(
        QStringLiteral("用户 #%1").arg(senderId));
    value.text = text;
    value.offline = offline;
    value.timestamp = QDateTime::fromString(
        message.value(QStringLiteral("time")).toString(),
        QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (!value.timestamp.isValid()) value.timestamp = QDateTime::currentDateTime();
    messages_[groupId].push_back(std::move(value));
    if (groupId != activeGroupId_)
    {
        const int count = unread_.value(groupId) + 1;
        unread_[groupId] = count;
        emit groupUnreadChanged(groupId, count);
    }
    emit groupMessagesChanged(groupId);
}

void SocialController::onRequestFinished(quint64 token, const QJsonObject &message)
{
    const auto it = pending_.find(token);
    if (it == pending_.end()) return;
    const Pending pending = it.value();
    pending_.erase(it);
    if (!message.value(QStringLiteral("ok")).toBool(false))
    {
        emit operationFailed(protocol::responseErrorText(message));
        return;
    }
    switch (pending.operation)
    {
    case Operation::QueryFriends:
        emit friendsChanged(message.value(QStringLiteral("friends")).toArray());
        break;
    case Operation::QueryGroups:
        emit groupsChanged(message.value(QStringLiteral("groups")).toArray());
        break;
    case Operation::QueryMembers:
        emit groupMembersChanged(
            message.value(QStringLiteral("group_id")).toInt(pending.subjectId),
            message.value(QStringLiteral("members")).toArray());
        break;
    case Operation::AddFriend:
        emit operationSucceeded(message.value(QStringLiteral("already_friends")).toBool()
                                    ? QStringLiteral("双方已经是好友")
                                    : QStringLiteral("好友添加成功"));
        refreshFriends();
        break;
    case Operation::CreateGroup:
        emit operationSucceeded(QStringLiteral("群组创建成功"));
        refreshGroups();
        break;
    case Operation::JoinGroup:
        emit operationSucceeded(message.value(QStringLiteral("already_member")).toBool()
                                    ? QStringLiteral("你已经在该群中")
                                    : QStringLiteral("已加入群组"));
        refreshGroups();
        break;
    case Operation::LeaveGroup:
        emit operationSucceeded(QStringLiteral("已退出群组"));
        refreshGroups();
        break;
    case Operation::GroupChat:
        if (!message.value(QStringLiteral("realtime_routed")).toBool(false))
            emit operationSucceeded(QStringLiteral("群消息已接收，部分成员将通过离线消息收到"));
        break;
    }
}

void SocialController::onRequestFailed(quint64 token, const QString &reason,
                                       const QJsonObject &)
{
    if (!pending_.remove(token)) return;
    emit operationFailed(reason);
}

} // namespace client
} // namespace nexusim
