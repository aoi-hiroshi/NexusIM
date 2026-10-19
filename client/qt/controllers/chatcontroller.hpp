#ifndef NEXUSIM_QT_CHATCONTROLLER_HPP
#define NEXUSIM_QT_CHATCONTROLLER_HPP

#include "models/conversationmodel.hpp"

#include <QJsonArray>
#include <QObject>
#include <QSet>
#include <QTimer>

namespace nexusim {
namespace client {

class NetworkClient;
class RequestManager;
class SessionController;

class ChatController : public QObject
{
    Q_OBJECT
public:
    explicit ChatController(NetworkClient *network, RequestManager *requests,
                            SessionController *session,
                            QObject *parent = nullptr);

    ConversationModel *conversations();
    void startSession(const QJsonObject &loginResponse);
    void stopSession();
    void openConversation(int userId, const QString &displayName = QString());
    bool sendMessage(int recipientId, const QString &displayName,
                     const QString &text);

signals:
    void activeConversationChanged(int userId);
    void contactsAvailable(const QJsonArray &contacts);
    void chatError(const QString &message);
    void connectionProblem(const QString &message);
    void messageAcceptedLocally();

private slots:
    void onMessage(const QJsonObject &message);
    void sendHeartbeat();
    void onRequestFinished(quint64 token, const QJsonObject &message);
    void onRequestFailed(quint64 token, const QString &reason,
                         const QJsonObject &message);

private:
    void appendIncoming(const QJsonObject &message, bool offline);
    QString displayNameFor(int userId, const QString &wireName) const;

    NetworkClient *network_;
    RequestManager *requests_;
    SessionController *session_;
    ConversationModel conversations_;
    QTimer heartbeatTimer_;
    quint64 heartbeatToken_{0};
    quint64 heartbeatSequence_{1};
    QSet<QByteArray> offlineFingerprints_;
    QHash<int, QString> knownNames_;
};

} // namespace client
} // namespace nexusim

#endif
