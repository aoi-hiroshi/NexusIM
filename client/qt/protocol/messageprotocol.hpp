#ifndef NEXUSIM_QT_MESSAGEPROTOCOL_HPP
#define NEXUSIM_QT_MESSAGEPROTOCOL_HPP

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace nexusim {
namespace client {
namespace protocol {

enum MessageType
{
    Login = 1,
    LoginAck = 2,
    Logout = 3,
    Register = 4,
    RegisterAck = 5,
    DirectChat = 6,
    AddFriend = 7,
    CreateGroup = 8,
    JoinGroup = 9,
    GroupChat = 10,
    Heartbeat = 11,
    HeartbeatAck = 12,
    Error = 13,
    AIChat = 14,
    AIChatAck = 15,
    AddFriendAck = 16,
    QueryFriends = 17,
    QueryFriendsAck = 18,
    CreateGroupAck = 19,
    JoinGroupAck = 20,
    QueryGroups = 21,
    QueryGroupsAck = 22,
    QueryGroupMembers = 23,
    QueryGroupMembersAck = 24,
    LeaveGroup = 25,
    LeaveGroupAck = 26,
    GroupChatAck = 27
};

QJsonObject loginRequest(int userId, const QString &password);
QJsonObject registerRequest(const QString &name, const QString &password);
QJsonObject logoutRequest();
QJsonObject directChatRequest(int recipientId, const QString &message,
                              const QString &timeText);
QJsonObject heartbeatRequest(const QString &requestId);
QJsonObject aiChatRequest(const QString &requestId,
                          const QString &conversationId,
                          const QString &prompt);
QJsonObject addFriendRequest(const QString &requestId, int friendId);
QJsonObject queryFriendsRequest(const QString &requestId);
QJsonObject createGroupRequest(const QString &requestId, const QString &name,
                               const QString &description);
QJsonObject joinGroupRequest(const QString &requestId, int groupId);
QJsonObject queryGroupsRequest(const QString &requestId);
QJsonObject queryGroupMembersRequest(const QString &requestId, int groupId);
QJsonObject leaveGroupRequest(const QString &requestId, int groupId);
QJsonObject groupChatRequest(const QString &requestId, int groupId,
                             const QString &message, const QString &timeText);
QString responseErrorText(const QJsonObject &object);
QVector<QJsonObject> decodeObjectStrings(const QJsonValue &value);

} // namespace protocol
} // namespace client
} // namespace nexusim

#endif
