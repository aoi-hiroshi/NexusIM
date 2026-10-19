#include "app/applicationcontroller.hpp"

#include "controllers/authcontroller.hpp"
#include "controllers/chatcontroller.hpp"
#include "controllers/sessioncontroller.hpp"
#include "network/requestmanager.hpp"
#include "ui/loginwindow.hpp"
#include "ui/mainwindow.hpp"

#include <QSettings>

namespace nexusim {
namespace client {

ApplicationController::ApplicationController(const ServerEndpoint &initialEndpoint,
                                             QObject *parent)
    : QObject(parent), network_(this), requests_(new RequestManager(this)),
      session_(new SessionController(this)),
      auth_(new AuthController(&network_, requests_.get(), session_.get(), this)),
      chat_(new ChatController(&network_, requests_.get(), session_.get(), this)),
      loginWindow_(new LoginWindow(auth_.get(), &network_, initialEndpoint)),
      mainWindow_(new MainWindow(chat_.get(), &network_))
{
    connect(&network_, &NetworkClient::messageReceived,
            requests_.get(), &RequestManager::handleMessage);
    connect(auth_.get(), &AuthController::loginSucceeded,
            this, &ApplicationController::onLoginSucceeded);
    connect(auth_.get(), &AuthController::loggedOut,
            this, &ApplicationController::onLoggedOut);
    connect(&network_, &NetworkClient::disconnected,
            this, &ApplicationController::onDisconnected);
    connect(&network_, &NetworkClient::protocolError,
            this, &ApplicationController::onProtocolError);
    connect(mainWindow_.get(), &MainWindow::logoutRequested,
            auth_.get(), &AuthController::logout);
    connect(mainWindow_.get(), &MainWindow::endpointChangeRequested,
            this, &ApplicationController::changeEndpoint);
    connect(chat_.get(), &ChatController::contactsAvailable,
            mainWindow_.get(), &MainWindow::showContacts);
    connect(chat_.get(), &ChatController::chatError,
            mainWindow_.get(), &MainWindow::showChatError);
    connect(chat_.get(), &ChatController::connectionProblem,
            mainWindow_.get(), &MainWindow::showChatError);
}

ApplicationController::~ApplicationController() = default;

void ApplicationController::start()
{
    loginWindow_->show();
}

void ApplicationController::onLoginSucceeded(const QJsonObject &response)
{
    chat_->startSession(response);
    mainWindow_->setSession(session_->session(), loginWindow_->endpoint());
    loginWindow_->hide();
    mainWindow_->show();
    mainWindow_->raise();
    mainWindow_->activateWindow();
}

void ApplicationController::onLoggedOut()
{
    chat_->stopSession();
    mainWindow_->reset();
    mainWindow_->hide();
    loginWindow_->showDisconnectedMessage(QStringLiteral("已安全退出登录"));
}

void ApplicationController::onDisconnected()
{
    requests_->cancelAll(QStringLiteral("连接已断开"));
    if (!session_->isAuthenticated()) return;
    chat_->stopSession();
    session_->clear();
    mainWindow_->reset();
    mainWindow_->hide();
    loginWindow_->showDisconnectedMessage(
        QStringLiteral("服务器连接已断开，请检查网络后重新登录"));
}

void ApplicationController::onProtocolError(const QString &message)
{
    if (mainWindow_->isVisible()) mainWindow_->showChatError(message);
    else loginWindow_->showDisconnectedMessage(message);
}

void ApplicationController::changeEndpoint(const ServerEndpoint &endpoint)
{
    loginWindow_->setEndpoint(endpoint);
    auth_->logout();
}

} // namespace client
} // namespace nexusim
