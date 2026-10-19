#ifndef NEXUSIM_QT_AUTHCONTROLLER_HPP
#define NEXUSIM_QT_AUTHCONTROLLER_HPP

#include "network/networkclient.hpp"

#include <QObject>

namespace nexusim {
namespace client {

class RequestManager;
class SessionController;

class AuthController : public QObject
{
    Q_OBJECT
public:
    explicit AuthController(NetworkClient *network, RequestManager *requests,
                            SessionController *session,
                            QObject *parent = nullptr);

    bool isBusy() const;
    void login(const ServerEndpoint &endpoint, int userId,
               const QString &password);
    void registerUser(const ServerEndpoint &endpoint, const QString &name,
                      const QString &password);
    void logout();
    void cancel();

signals:
    void busyChanged(bool busy);
    void loginSucceeded(const QJsonObject &response);
    void registrationSucceeded(int userId);
    void operationFailed(const QString &message);
    void loggedOut();

private slots:
    void sendPendingOperation();
    void onRequestFinished(quint64 token, const QJsonObject &message);
    void onRequestFailed(quint64 token, const QString &reason,
                         const QJsonObject &message);
    void onNetworkError(const QString &message);

private:
    enum class Operation { None, Login, Register };
    void setBusy(bool busy);
    void fail(const QString &message);

    NetworkClient *network_;
    RequestManager *requests_;
    SessionController *session_;
    ServerEndpoint endpoint_;
    Operation operation_{Operation::None};
    int pendingUserId_{-1};
    QString pendingName_;
    QString pendingPassword_;
    quint64 requestToken_{0};
    bool busy_{false};
};

} // namespace client
} // namespace nexusim

#endif
