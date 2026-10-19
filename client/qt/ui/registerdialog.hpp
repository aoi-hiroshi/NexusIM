#ifndef NEXUSIM_QT_REGISTERDIALOG_HPP
#define NEXUSIM_QT_REGISTERDIALOG_HPP

#include "network/networkclient.hpp"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace nexusim {
namespace client {

class AuthController;

class RegisterDialog : public QDialog
{
    Q_OBJECT
public:
    RegisterDialog(AuthController *auth, const ServerEndpoint &endpoint,
                   QWidget *parent = nullptr);

signals:
    void registrationComplete(int userId);

private slots:
    void submit();
    void onSucceeded(int userId);
    void onFailed(const QString &message);
    void setBusy(bool busy);

private:
    AuthController *auth_;
    ServerEndpoint endpoint_;
    QLineEdit *nameEdit_;
    QLineEdit *passwordEdit_;
    QLineEdit *confirmEdit_;
    QLabel *statusLabel_;
    QPushButton *submitButton_;
};

} // namespace client
} // namespace nexusim

#endif
