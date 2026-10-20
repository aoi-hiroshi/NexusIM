#ifndef NEXUSIM_QT_AICONTROLLER_HPP
#define NEXUSIM_QT_AICONTROLLER_HPP

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace nexusim {
namespace client {

class NetworkClient;
class RequestManager;
class SessionController;

class AIController : public QObject
{
    Q_OBJECT
public:
    explicit AIController(NetworkClient *network, RequestManager *requests,
                          SessionController *session,
                          int requestTimeoutMs = 90000,
                          QObject *parent = nullptr);

    QString conversationId() const;
    bool isBusy() const;
    void startSession();
    void stopSession();

public slots:
    void sendPrompt(const QString &prompt);
    void newConversation();

signals:
    void conversationChanged(const QString &conversationId);
    void promptAccepted(const QString &requestId, const QString &prompt);
    void replyReceived(const QString &requestId, const QString &content,
                       const QString &model, const QString &finishReason,
                       qint64 elapsedMs, const QString &warning);
    void requestFailed(const QString &requestId, const QString &code,
                       const QString &message);
    void busyChanged(bool busy);

private slots:
    void onRequestFinished(quint64 token, const QJsonObject &message);
    void onRequestFailed(quint64 token, const QString &reason,
                         const QJsonObject &message);

private:
    struct Pending
    {
        QString requestId;
        QString conversationId;
        qint64 startedMs{0};
    };

    void cancelPending(const QString &reason);
    void updateBusy();
    QString createConversationId() const;

    NetworkClient *network_;
    RequestManager *requests_;
    SessionController *session_;
    int requestTimeoutMs_;
    QString conversationId_;
    QHash<quint64, Pending> pending_;
    bool busy_{false};
};

} // namespace client
} // namespace nexusim

#endif
