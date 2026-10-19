#ifndef NEXUSIM_QT_REQUESTMANAGER_HPP
#define NEXUSIM_QT_REQUESTMANAGER_HPP

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QTimer>

namespace nexusim {
namespace client {

class RequestManager : public QObject
{
    Q_OBJECT
public:
    explicit RequestManager(QObject *parent = nullptr);

    quint64 startRequest(int requestMsgId, int responseMsgId, int timeoutMs,
                         const QString &correlationId = QString());
    bool cancelRequest(quint64 token, const QString &reason);
    void cancelAll(const QString &reason);
    int pendingCount() const;

public slots:
    void handleMessage(const QJsonObject &message);

signals:
    void requestFinished(quint64 token, const QJsonObject &message);
    void requestFailed(quint64 token, const QString &reason,
                       const QJsonObject &message);

private:
    struct Pending
    {
        int requestMsgId{0};
        int responseMsgId{0};
        QString correlationId;
        QTimer *timer{nullptr};
    };

    quint64 matchingToken(int requestMsgId, int responseMsgId,
                          const QString &correlationId) const;
    Pending take(quint64 token);

    QHash<quint64, Pending> pending_;
    quint64 nextToken_{1};
};

} // namespace client
} // namespace nexusim

#endif
