#ifndef NEXUSIM_QT_LOGINWINDOW_HPP
#define NEXUSIM_QT_LOGINWINDOW_HPP

#include "network/networkclient.hpp"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace nexusim {
namespace client {

class AuthController;

class LoginWindow : public QWidget
{
    Q_OBJECT
public:
    LoginWindow(AuthController *auth, NetworkClient *network,
                const ServerEndpoint &endpoint, QWidget *parent = nullptr);

    ServerEndpoint endpoint() const;
    void setEndpoint(const ServerEndpoint &endpoint);
    void showDisconnectedMessage(const QString &message);

private slots:
    void submitLogin();
    void openRegister();
    void openSettings();
    void setBusy(bool busy);
    void showError(const QString &message);
    void updateNetworkState(NetworkClient::State state);

private:
    void updateEndpointLabel();

    AuthController *auth_;
    NetworkClient *network_;
    ServerEndpoint endpoint_;
    QLineEdit *idEdit_;
    QLineEdit *passwordEdit_;
    QPushButton *loginButton_;
    QLabel *statusLabel_;
    QLabel *endpointLabel_;
    QLabel *networkLabel_;
};

} // namespace client
} // namespace nexusim

#endif
