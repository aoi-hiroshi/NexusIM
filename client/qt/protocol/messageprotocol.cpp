#include "protocol/messageprotocol.hpp"

#include <QJsonDocument>

namespace nexusim {
namespace client {
namespace protocol {

QJsonObject loginRequest(int userId, const QString &password)
{
    return {{QStringLiteral("msgid"), Login},
            {QStringLiteral("id"), userId},
            {QStringLiteral("password"), password}};
}

QJsonObject registerRequest(const QString &name, const QString &password)
{
    return {{QStringLiteral("msgid"), Register},
            {QStringLiteral("name"), name},
            {QStringLiteral("password"), password}};
}

QJsonObject logoutRequest()
{
    return {{QStringLiteral("msgid"), Logout}};
}

QJsonObject directChatRequest(int recipientId, const QString &message,
                              const QString &timeText)
{
    return {{QStringLiteral("msgid"), DirectChat},
            {QStringLiteral("toid"), recipientId},
            {QStringLiteral("msg"), message},
            {QStringLiteral("time"), timeText}};
}

QJsonObject heartbeatRequest(const QString &requestId)
{
    return {{QStringLiteral("msgid"), Heartbeat},
            {QStringLiteral("request_id"), requestId}};
}

QJsonObject aiChatRequest(const QString &requestId,
                          const QString &conversationId,
                          const QString &prompt)
{
    return {{QStringLiteral("msgid"), AIChat},
            {QStringLiteral("request_id"), requestId},
            {QStringLiteral("conversation_id"), conversationId},
            {QStringLiteral("prompt"), prompt}};
}

QJsonObject addFriendRequest(const QString &requestId, int friendId)
{
    return {{QStringLiteral("msgid"), AddFriend},
            {QStringLiteral("request_id"), requestId},
            {QStringLiteral("friendid"), friendId}};
}

QJsonObject queryFriendsRequest(const QString &requestId)
{
    return {{QStringLiteral("msgid"), QueryFriends},
            {QStringLiteral("request_id"), requestId}};
}

QJsonObject createGroupRequest(const QString &requestId, const QString &name,
                               const QString &description)
{
    return {{QStringLiteral("msgid"), CreateGroup},
            {QStringLiteral("request_id"), requestId},
            {QStringLiteral("groupname"), name},
            {QStringLiteral("groupdesc"), description}};
}

QJsonObject joinGroupRequest(const QString &requestId, int groupId)
{
    return {{QStringLiteral("msgid"), JoinGroup},
            {QStringLiteral("request_id"), requestId},
            {QStringLiteral("groupid"), groupId}};
}

QJsonObject queryGroupsRequest(const QString &requestId)
{
    return {{QStringLiteral("msgid"), QueryGroups},
            {QStringLiteral("request_id"), requestId}};
}

QJsonObject queryGroupMembersRequest(const QString &requestId, int groupId)
{
    return {{QStringLiteral("msgid"), QueryGroupMembers},
            {QStringLiteral("request_id"), requestId},
            {QStringLiteral("groupid"), groupId}};
}

QJsonObject leaveGroupRequest(const QString &requestId, int groupId)
{
    return {{QStringLiteral("msgid"), LeaveGroup},
            {QStringLiteral("request_id"), requestId},
            {QStringLiteral("groupid"), groupId}};
}

QJsonObject groupChatRequest(const QString &requestId, int groupId,
                             const QString &message, const QString &timeText)
{
    return {{QStringLiteral("msgid"), GroupChat},
            {QStringLiteral("request_id"), requestId},
            {QStringLiteral("groupid"), groupId},
            {QStringLiteral("msg"), message},
            {QStringLiteral("time"), timeText}};
}

QString responseErrorText(const QJsonObject &object)
{
    const QString message = object.value(QStringLiteral("errmsg")).toString();
    const QString code = object.value(QStringLiteral("error")).toString();
    if (message.isEmpty()) return code.isEmpty() ? QStringLiteral("服务器返回未知错误") : code;
    return code.isEmpty() ? message : QStringLiteral("%1（%2）").arg(message, code);
}

QVector<QJsonObject> decodeObjectStrings(const QJsonValue &value)
{
    QVector<QJsonObject> result;
    if (!value.isArray()) return result;
    const QJsonArray array = value.toArray();
    result.reserve(array.size());
    for (const QJsonValue &entry : array)
    {
        if (entry.isObject())
        {
            result.push_back(entry.toObject());
            continue;
        }
        if (!entry.isString()) continue;
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(
            entry.toString().toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && document.isObject())
            result.push_back(document.object());
    }
    return result;
}

} // namespace protocol
} // namespace client
} // namespace nexusim
