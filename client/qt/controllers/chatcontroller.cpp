#include "controllers/chatcontroller.hpp"

#include "controllers/sessioncontroller.hpp"
#include "network/networkclient.hpp"
#include "network/requestmanager.hpp"
#include "protocol/messageprotocol.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>

namespace nexusim {
namespace client {

ChatController::ChatController(NetworkClient *network, RequestManager *requests,
                               SessionController *session, QObject *parent)
    : QObject(parent), network_(network), requests_(requests), session_(session),
      conversations_(this)
{
    heartbeatTimer_.setInterval(30000);
    connect(&heartbeatTimer_, &QTimer::timeout,
            this, &ChatController::sendHeartbeat);
    connect(network_, &NetworkClient::messageReceived,
            this, &ChatController::onMessage);
    connect(requests_, &RequestManager::requestFinished,
            this, &ChatController::onRequestFinished);
    connect(requests_, &RequestManager::requestFailed,
            this, &ChatController::onRequestFailed);
}

ConversationModel *ChatController::conversations() { return &conversations_; }

void ChatController::startSession(const QJsonObject &loginResponse)
{
    conversations_.clear();
    knownNames_.clear();
    offlineFingerprints_.clear();

    QJsonArray contacts;
    const QVector<QJsonObject> friends = protocol::decodeObjectStrings(
        loginResponse.value(QStringLiteral("friends")));
    for (const QJsonObject &friendObject : friends)
    {
        const int id = friendObject.value(QStringLiteral("id")).toInt(-1);
        if (id <= 0) continue;
        const QString name = friendObject.value(QStringLiteral("name")).toString();
        knownNames_[id] = name;
        QJsonObject safeContact{{QStringLiteral("id"), id},
                                {QStringLiteral("name"), name}};
        contacts.push_back(safeContact);
    }
    emit contactsAvailable(contacts);

    const QVector<QJsonObject> offline = protocol::decodeObjectStrings(
        loginResponse.value(QStringLiteral("offlinemsg")));
    for (const QJsonObject &entry : offline)
    {
        const QByteArray canonical = QJsonDocument(entry).toJson(QJsonDocument::Compact);
        const QByteArray fingerprint = QCryptographicHash::hash(
            canonical, QCryptographicHash::Sha256);
        if (offlineFingerprints_.contains(fingerprint)) continue;
        offlineFingerprints_.insert(fingerprint);
        appendIncoming(entry, true);
    }
    heartbeatTimer_.start();
}

void ChatController::stopSession()
{
    heartbeatTimer_.stop();
    if (heartbeatToken_)
        requests_->cancelRequest(heartbeatToken_, QStringLiteral("会话已结束"));
    heartbeatToken_ = 0;
    conversations_.clear();
    knownNames_.clear();
    offlineFingerprints_.clear();
}

void ChatController::openConversation(int userId, const QString &displayName)
{
    if (userId <= 0) return;
    const QString name = displayNameFor(userId, displayName);
    knownNames_[userId] = name;
    conversations_.ensureConversation(userId, name);
    conversations_.setActiveConversation(userId);
    emit activeConversationChanged(userId);
}

bool ChatController::sendMessage(int recipientId, const QString &displayName,
                                 const QString &text)
{
    if (!session_->isAuthenticated())
    {
        emit chatError(QStringLiteral("请先登录"));
        return false;
    }
    if (recipientId <= 0 || recipientId == session_->session().userId)
    {
        emit chatError(QStringLiteral("目标用户无效，且不能给自己发送消息"));
        return false;
    }
    const QString normalized = text.trimmed();
    if (normalized.isEmpty() || normalized.toUtf8().size() > 65536)
    {
        emit chatError(QStringLiteral("消息不能为空且不能超过 65536 个 UTF-8 字节"));
        return false;
    }
    const QDateTime now = QDateTime::currentDateTime();
    QString error;
    if (!network_->sendJson(protocol::directChatRequest(
                                recipientId, normalized,
                                now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))),
                            &error))
    {
        emit chatError(error);
        return false;
    }
    ChatMessage message;
    message.text = normalized;
    message.senderName = session_->session().userName;
    message.timestamp = now;
    message.outgoing = true;
    conversations_.appendMessage(recipientId,
                                 displayNameFor(recipientId, displayName),
                                 message, false);
    emit messageAcceptedLocally();
    return true;
}

void ChatController::setContacts(const QJsonArray &contacts)
{
    for (const QJsonValue &value : contacts)
    {
        const QJsonObject contact = value.toObject();
        const int id = contact.value(QStringLiteral("id")).toInt(-1);
        if (id <= 0) continue;
        const QString name = contact.value(QStringLiteral("name")).toString();
        if (!name.isEmpty()) knownNames_[id] = name;
    }
    emit contactsAvailable(contacts);
}

void ChatController::onMessage(const QJsonObject &message)
{
    const int type = message.value(QStringLiteral("msgid")).toInt(-1);
    if (type == protocol::DirectChat)
    {
        appendIncoming(message, false);
        return;
    }
    if (type == protocol::Error &&
        message.value(QStringLiteral("request_msgid")).toInt(-1) == protocol::DirectChat)
    {
        const int recipient = message.value(QStringLiteral("toid")).toInt(-1);
        const QString error = protocol::responseErrorText(message);
        if (recipient > 0)
        {
            ChatMessage notice;
            notice.text = error;
            notice.timestamp = QDateTime::currentDateTime();
            notice.system = true;
            conversations_.appendMessage(recipient, displayNameFor(recipient, QString()),
                                         notice, false);
        }
        emit chatError(error);
    }
}

void ChatController::sendHeartbeat()
{
    if (!session_->isAuthenticated() || !network_->isConnected() || heartbeatToken_)
        return;
    const QString requestId = QStringLiteral("qt-%1-%2")
                                  .arg(session_->session().userId)
                                  .arg(heartbeatSequence_++);
    heartbeatToken_ = requests_->startRequest(protocol::Heartbeat,
                                               protocol::HeartbeatAck,
                                               10000, requestId);
    if (!heartbeatToken_) return;
    QString error;
    if (!network_->sendJson(protocol::heartbeatRequest(requestId), &error))
    {
        const quint64 token = heartbeatToken_;
        heartbeatToken_ = 0;
        requests_->cancelRequest(token, error);
    }
}

void ChatController::onRequestFinished(quint64 token, const QJsonObject &message)
{
    if (token != heartbeatToken_) return;
    heartbeatToken_ = 0;
    if (!message.value(QStringLiteral("ok")).toBool(false))
        emit connectionProblem(QStringLiteral("在线会话续租失败，请重新登录"));
}

void ChatController::onRequestFailed(quint64 token, const QString &reason,
                                     const QJsonObject &)
{
    if (token != heartbeatToken_) return;
    heartbeatToken_ = 0;
    emit connectionProblem(QStringLiteral("心跳失败：%1").arg(reason));
}

void ChatController::appendIncoming(const QJsonObject &message, bool offline)
{
    if (message.value(QStringLiteral("msgid")).toInt(-1) != protocol::DirectChat)
        return;
    const int sender = message.value(QStringLiteral("id")).toInt(-1);
    const QString text = message.value(QStringLiteral("msg")).toString();
    if (sender <= 0 || text.isEmpty()) return;
    const QString wireName = message.value(QStringLiteral("name")).toString();
    const QString name = displayNameFor(sender, wireName);
    knownNames_[sender] = name;
    ChatMessage chat;
    chat.text = text;
    chat.senderName = name;
    chat.outgoing = false;
    chat.offline = offline;
    const QString wireTime = message.value(QStringLiteral("time")).toString();
    chat.timestamp = QDateTime::fromString(wireTime, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (!chat.timestamp.isValid()) chat.timestamp = QDateTime::currentDateTime();
    conversations_.appendMessage(sender, name, chat, true);
}

QString ChatController::displayNameFor(int userId, const QString &wireName) const
{
    if (!wireName.trimmed().isEmpty()) return wireName.trimmed();
    const auto existing = knownNames_.constFind(userId);
    if (existing != knownNames_.constEnd() && !existing.value().isEmpty())
        return existing.value();
    return QStringLiteral("用户 #%1").arg(userId);
}

} // namespace client
} // namespace nexusim
