#ifndef NEXUSIM_QT_APPLICATIONCONTROLLER_HPP
#define NEXUSIM_QT_APPLICATIONCONTROLLER_HPP

#include "network/networkclient.hpp"

#include <QObject>
#include <memory>

namespace nexusim {
namespace client {

class AuthController;
class AIController;
class ChatController;
class LoginWindow;
class MainWindow;
class RequestManager;
class SessionController;

class ApplicationController : public QObject
{
    Q_OBJECT
public:
    explicit ApplicationController(const ServerEndpoint &initialEndpoint,
                                   QObject *parent = nullptr);
    ~ApplicationController() override;
    void start();

private slots:
    void onLoginSucceeded(const QJsonObject &response);
    void onLoggedOut();
    void onDisconnected();
    void onProtocolError(const QString &message);
    void changeEndpoint(const ServerEndpoint &endpoint);

private:
    NetworkClient network_;
    std::unique_ptr<RequestManager> requests_;
    std::unique_ptr<SessionController> session_;
    std::unique_ptr<AuthController> auth_;
    std::unique_ptr<ChatController> chat_;
    std::unique_ptr<AIController> ai_;
    std::unique_ptr<LoginWindow> loginWindow_;
    std::unique_ptr<MainWindow> mainWindow_;
};

} // namespace client
} // namespace nexusim

#endif
