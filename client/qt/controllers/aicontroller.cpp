#include "controllers/aicontroller.hpp"

#include "controllers/sessioncontroller.hpp"
#include "network/networkclient.hpp"
#include "network/requestmanager.hpp"
#include "protocol/messageprotocol.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QUuid>

namespace nexusim {
namespace client {

AIController::AIController(NetworkClient *network, RequestManager *requests,
                           SessionController *session, int requestTimeoutMs,
                           QObject *parent)
    : QObject(parent), network_(network), requests_(requests), session_(session),
      requestTimeoutMs_(qBound(1000, requestTimeoutMs, 180000))
{
    connect(requests_, &RequestManager::requestFinished,
            this, &AIController::onRequestFinished);
    connect(requests_, &RequestManager::requestFailed,
            this, &AIController::onRequestFailed);
}

QString AIController::conversationId() const { return conversationId_; }
bool AIController::isBusy() const { return busy_; }

void AIController::startSession()
{
    cancelPending(QStringLiteral("AI 会话已重置"));
    conversationId_ = createConversationId();
    emit conversationChanged(conversationId_);
}

void AIController::stopSession()
{
    cancelPending(QStringLiteral("AI 会话已结束"));
    conversationId_.clear();
    emit conversationChanged(QString());
}

void AIController::sendPrompt(const QString &prompt)
{
    if (!session_->isAuthenticated())
    {
        emit requestFailed(QString(), QStringLiteral("UNAUTHENTICATED"),
                           QStringLiteral("请先登录后再使用 AI 助手"));
        return;
    }
    if (!network_->isConnected())
    {
        emit requestFailed(QString(), QStringLiteral("NETWORK_DISCONNECTED"),
                           QStringLiteral("服务器连接已断开"));
        return;
    }
    if (!pending_.isEmpty())
    {
        emit requestFailed(QString(), QStringLiteral("AI_SERVICE_BUSY"),
                           QStringLiteral("上一条 AI 请求仍在处理中"));
        return;
    }
    const QString normalized = prompt.trimmed();
    if (normalized.isEmpty() || normalized.toUtf8().size() > 32768)
    {
        emit requestFailed(QString(), QStringLiteral("AI_INVALID_ARGUMENT"),
                           QStringLiteral("问题不能为空且不能超过 32768 个 UTF-8 字节"));
        return;
    }
    if (conversationId_.isEmpty()) conversationId_ = createConversationId();
    const QString requestId = QStringLiteral("ai-%1-%2")
                                  .arg(session_->session().userId)
                                  .arg(QUuid::createUuid().toString(
                                      QUuid::WithoutBraces));
    const quint64 token = requests_->startRequest(
        protocol::AIChat, protocol::AIChatAck, requestTimeoutMs_, requestId);
    if (!token)
    {
        emit requestFailed(requestId, QStringLiteral("AI_SERVICE_BUSY"),
                           QStringLiteral("无法登记 AI 请求"));
        return;
    }
    Pending pending;
    pending.requestId = requestId;
    pending.conversationId = conversationId_;
    pending.startedMs = QDateTime::currentMSecsSinceEpoch();
    pending_.insert(token, pending);
    QString error;
    if (!network_->sendJson(protocol::aiChatRequest(
                                requestId, conversationId_, normalized),
                            &error))
    {
        pending_.remove(token);
        requests_->cancelRequest(token, error);
        emit requestFailed(requestId, QStringLiteral("NETWORK_ERROR"), error);
        updateBusy();
        return;
    }
    updateBusy();
    emit promptAccepted(requestId, normalized);
}

void AIController::newConversation()
{
    if (!session_->isAuthenticated()) return;
    cancelPending(QStringLiteral("已开始新的 AI 会话"));
    conversationId_ = createConversationId();
    emit conversationChanged(conversationId_);
}

void AIController::onRequestFinished(quint64 token,
                                     const QJsonObject &message)
{
    const auto iterator = pending_.find(token);
    if (iterator == pending_.end()) return;
    const Pending pending = iterator.value();
    pending_.erase(iterator);
    updateBusy();
    if (message.value(QStringLiteral("request_id")).toString() !=
            pending.requestId ||
        message.value(QStringLiteral("conversation_id")).toString() !=
            pending.conversationId ||
        pending.conversationId != conversationId_)
    {
        emit requestFailed(pending.requestId,
                           QStringLiteral("AI_REQUEST_CANCELLED"),
                           QStringLiteral("忽略了不属于当前会话的 AI 响应"));
        return;
    }
    if (!message.value(QStringLiteral("ok")).toBool(false))
    {
        emit requestFailed(
            pending.requestId,
            message.value(QStringLiteral("error")).toString(
                QStringLiteral("AI_UPSTREAM_ERROR")),
            protocol::responseErrorText(message));
        return;
    }
    const QString content = message.value(QStringLiteral("content")).toString();
    if (content.trimmed().isEmpty())
    {
        emit requestFailed(pending.requestId,
                           QStringLiteral("AI_INVALID_RESPONSE"),
                           QStringLiteral("AI 返回了空内容"));
        return;
    }
    emit replyReceived(
        pending.requestId, content,
        message.value(QStringLiteral("model")).toString(
            QStringLiteral("DeepSeek")),
        message.value(QStringLiteral("finish_reason")).toString(),
        QDateTime::currentMSecsSinceEpoch() - pending.startedMs,
        message.value(QStringLiteral("warning")).toString());
}

void AIController::onRequestFailed(quint64 token, const QString &reason,
                                   const QJsonObject &message)
{
    const auto iterator = pending_.find(token);
    if (iterator == pending_.end()) return;
    const Pending pending = iterator.value();
    pending_.erase(iterator);
    updateBusy();
    const QString code = message.value(QStringLiteral("error")).toString(
        reason == QStringLiteral("请求超时")
            ? QStringLiteral("AI_UPSTREAM_TIMEOUT")
            : QStringLiteral("AI_REQUEST_CANCELLED"));
    emit requestFailed(pending.requestId, code, reason);
}

void AIController::cancelPending(const QString &reason)
{
    const QList<quint64> tokens = pending_.keys();
    pending_.clear();
    for (quint64 token : tokens) requests_->cancelRequest(token, reason);
    updateBusy();
}

void AIController::updateBusy()
{
    const bool value = !pending_.isEmpty();
    if (value == busy_) return;
    busy_ = value;
    emit busyChanged(busy_);
}

QString AIController::createConversationId() const
{
    return QStringLiteral("user-%1-%2")
        .arg(session_->session().userId)
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

} // namespace client
} // namespace nexusim
