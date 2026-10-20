#include "network/requestmanager.hpp"

#include "protocol/messageprotocol.hpp"

namespace nexusim {
namespace client {

RequestManager::RequestManager(QObject *parent) : QObject(parent) {}

quint64 RequestManager::startRequest(int requestMsgId, int responseMsgId,
                                     int timeoutMs,
                                     const QString &correlationId)
{
    for (auto it = pending_.constBegin(); it != pending_.constEnd(); ++it)
    {
        if (it->requestMsgId != requestMsgId &&
            it->responseMsgId != responseMsgId)
            continue;
        // Legacy Login/Register requests have no correlation id and therefore
        // remain strictly single-flight. Correlated AI/heartbeat requests can
        // safely coexist as long as their ids are distinct.
        if (correlationId.isEmpty() || it->correlationId.isEmpty() ||
            it->correlationId == correlationId)
            return 0;
    }
    const quint64 token = nextToken_++;
    Pending value;
    value.requestMsgId = requestMsgId;
    value.responseMsgId = responseMsgId;
    value.correlationId = correlationId;
    value.timer = new QTimer(this);
    value.timer->setSingleShot(true);
    connect(value.timer, &QTimer::timeout, this, [this, token] {
        if (!pending_.contains(token)) return;
        take(token);
        emit requestFailed(token, QStringLiteral("请求超时"), QJsonObject());
    });
    pending_.insert(token, value);
    value.timer->start(qMax(1, timeoutMs));
    return token;
}

bool RequestManager::cancelRequest(quint64 token, const QString &reason)
{
    if (!pending_.contains(token)) return false;
    take(token);
    emit requestFailed(token, reason, QJsonObject());
    return true;
}

void RequestManager::cancelAll(const QString &reason)
{
    const QList<quint64> tokens = pending_.keys();
    for (quint64 token : tokens) cancelRequest(token, reason);
}

int RequestManager::pendingCount() const { return pending_.size(); }

void RequestManager::handleMessage(const QJsonObject &message)
{
    const int msgid = message.value(QStringLiteral("msgid")).toInt(-1);
    quint64 token = 0;
    if (msgid == protocol::Error)
    {
        const int requestId = message.value(QStringLiteral("request_msgid")).toInt(-1);
        const QString correlation = message.value(
            QStringLiteral("request_id")).toString();
        token = matchingToken(requestId, -1, correlation);
        if (!token) return;
        take(token);
        emit requestFailed(token, protocol::responseErrorText(message), message);
        return;
    }

    const QString correlation = message.value(QStringLiteral("request_id")).toString();
    token = matchingToken(-1, msgid, correlation);
    if (!token) return;
    take(token);
    emit requestFinished(token, message);
}

quint64 RequestManager::matchingToken(int requestMsgId, int responseMsgId,
                                      const QString &correlationId) const
{
    for (auto it = pending_.constBegin(); it != pending_.constEnd(); ++it)
    {
        if (requestMsgId >= 0 && it->requestMsgId != requestMsgId) continue;
        if (responseMsgId >= 0 && it->responseMsgId != responseMsgId) continue;
        if (!it->correlationId.isEmpty() && it->correlationId != correlationId)
            continue;
        return it.key();
    }
    return 0;
}

RequestManager::Pending RequestManager::take(quint64 token)
{
    Pending value = pending_.take(token);
    if (value.timer)
    {
        value.timer->stop();
        value.timer->deleteLater();
        value.timer = nullptr;
    }
    return value;
}

} // namespace client
} // namespace nexusim
