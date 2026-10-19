#include "controllers/authcontroller.hpp"

#include "controllers/sessioncontroller.hpp"
#include "network/requestmanager.hpp"
#include "protocol/messageprotocol.hpp"

namespace nexusim {
namespace client {

AuthController::AuthController(NetworkClient *network, RequestManager *requests,
                               SessionController *session, QObject *parent)
    : QObject(parent), network_(network), requests_(requests), session_(session)
{
    connect(network_, &NetworkClient::connected,
            this, &AuthController::sendPendingOperation);
    connect(network_, &NetworkClient::networkError,
            this, &AuthController::onNetworkError);
    connect(requests_, &RequestManager::requestFinished,
            this, &AuthController::onRequestFinished);
    connect(requests_, &RequestManager::requestFailed,
            this, &AuthController::onRequestFailed);
}

bool AuthController::isBusy() const { return busy_; }

void AuthController::login(const ServerEndpoint &endpoint, int userId,
                           const QString &password)
{
    if (busy_) return;
    if (userId <= 0 || password.isEmpty())
    {
        emit operationFailed(QStringLiteral("请输入有效的用户 ID 和密码"));
        return;
    }
    endpoint_ = endpoint;
    operation_ = Operation::Login;
    pendingUserId_ = userId;
    pendingPassword_ = password;
    pendingName_.clear();
    setBusy(true);
    if (network_->isConnected() && network_->endpoint().host == endpoint.host &&
        network_->endpoint().port == endpoint.port)
        sendPendingOperation();
    else
        network_->connectToServer(endpoint);
}

void AuthController::registerUser(const ServerEndpoint &endpoint,
                                  const QString &name,
                                  const QString &password)
{
    if (busy_) return;
    const int nameBytes = name.toUtf8().size();
    const int passwordBytes = password.toUtf8().size();
    if (nameBytes < 1 || nameBytes > 50 || passwordBytes < 8 || passwordBytes > 128)
    {
        emit operationFailed(QStringLiteral("用户名需为 1–50 字节，密码需为 8–128 字节"));
        return;
    }
    endpoint_ = endpoint;
    operation_ = Operation::Register;
    pendingName_ = name;
    pendingPassword_ = password;
    pendingUserId_ = -1;
    setBusy(true);
    if (network_->isConnected() && network_->endpoint().host == endpoint.host &&
        network_->endpoint().port == endpoint.port)
        sendPendingOperation();
    else
        network_->connectToServer(endpoint);
}

void AuthController::logout()
{
    cancel();
    if (session_->isAuthenticated() && network_->isConnected())
    {
        QString ignored;
        network_->sendJson(protocol::logoutRequest(), &ignored);
    }
    session_->clear();
    requests_->cancelAll(QStringLiteral("用户已注销"));
    network_->disconnectFromServer();
    emit loggedOut();
}

void AuthController::cancel()
{
    if (requestToken_) requests_->cancelRequest(requestToken_, QStringLiteral("操作已取消"));
    requestToken_ = 0;
    operation_ = Operation::None;
    pendingPassword_.clear();
    setBusy(false);
}

void AuthController::sendPendingOperation()
{
    if (!busy_ || operation_ == Operation::None || requestToken_) return;
    const int requestType = operation_ == Operation::Login
                                ? protocol::Login : protocol::Register;
    const int responseType = operation_ == Operation::Login
                                 ? protocol::LoginAck : protocol::RegisterAck;
    requestToken_ = requests_->startRequest(requestType, responseType,
                                             endpoint_.requestTimeoutMs);
    if (!requestToken_)
    {
        fail(QStringLiteral("已有相同类型的请求正在处理中"));
        return;
    }
    const QJsonObject request = operation_ == Operation::Login
                                    ? protocol::loginRequest(pendingUserId_, pendingPassword_)
                                    : protocol::registerRequest(pendingName_, pendingPassword_);
    QString error;
    if (!network_->sendJson(request, &error))
    {
        const quint64 token = requestToken_;
        requestToken_ = 0;
        requests_->cancelRequest(token, error);
    }
}

void AuthController::onRequestFinished(quint64 token,
                                       const QJsonObject &message)
{
    if (token != requestToken_) return;
    requestToken_ = 0;
    const Operation finished = operation_;
    operation_ = Operation::None;
    pendingPassword_.clear();
    setBusy(false);
    if (message.value(QStringLiteral("errno")).toInt(-1) != 0)
    {
        emit operationFailed(protocol::responseErrorText(message));
        return;
    }
    if (finished == Operation::Login)
    {
        session_->establish(message);
        if (!session_->isAuthenticated())
        {
            emit operationFailed(QStringLiteral("登录响应缺少有效会话信息"));
            return;
        }
        emit loginSucceeded(message);
    }
    else if (finished == Operation::Register)
    {
        const int id = message.value(QStringLiteral("id")).toInt(-1);
        if (id <= 0) emit operationFailed(QStringLiteral("注册响应缺少有效用户 ID"));
        else emit registrationSucceeded(id);
    }
}

void AuthController::onRequestFailed(quint64 token, const QString &reason,
                                     const QJsonObject &message)
{
    if (token != requestToken_) return;
    requestToken_ = 0;
    // Login/Register responses in the legacy wire protocol have no request_id.
    // After a local timeout or cancellation, discard this TCP stream so a late
    // response cannot be mistaken for a later authentication attempt.
    if (message.isEmpty() && network_->isConnected())
        network_->disconnectFromServer();
    fail(reason);
}

void AuthController::onNetworkError(const QString &message)
{
    if (busy_ && requestToken_ == 0) fail(message);
}

void AuthController::setBusy(bool busy)
{
    if (busy_ == busy) return;
    busy_ = busy;
    emit busyChanged(busy_);
}

void AuthController::fail(const QString &message)
{
    operation_ = Operation::None;
    pendingPassword_.clear();
    setBusy(false);
    emit operationFailed(message);
}

} // namespace client
} // namespace nexusim
