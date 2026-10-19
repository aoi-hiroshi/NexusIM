#include "ui/registerdialog.hpp"

#include "controllers/authcontroller.hpp"

#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace nexusim {
namespace client {

RegisterDialog::RegisterDialog(AuthController *auth,
                               const ServerEndpoint &endpoint, QWidget *parent)
    : QDialog(parent), auth_(auth), endpoint_(endpoint),
      nameEdit_(new QLineEdit(this)), passwordEdit_(new QLineEdit(this)),
      confirmEdit_(new QLineEdit(this)), statusLabel_(new QLabel(this)),
      submitButton_(new QPushButton(QStringLiteral("创建账号"), this))
{
    setWindowTitle(QStringLiteral("注册 NexusIM"));
    setModal(true);
    setMinimumWidth(420);
    nameEdit_->setPlaceholderText(QStringLiteral("1–50 个 UTF-8 字节"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setPlaceholderText(QStringLiteral("8–128 个 UTF-8 字节"));
    confirmEdit_->setEchoMode(QLineEdit::Password);
    statusLabel_->setObjectName(QStringLiteral("errorLabel"));
    statusLabel_->setWordWrap(true);
    submitButton_->setObjectName(QStringLiteral("primaryButton"));

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("用户名"), nameEdit_);
    form->addRow(QStringLiteral("密码"), passwordEdit_);
    form->addRow(QStringLiteral("确认密码"), confirmEdit_);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(14);
    layout->addLayout(form);
    layout->addWidget(statusLabel_);
    layout->addWidget(submitButton_);

    connect(submitButton_, &QPushButton::clicked, this, &RegisterDialog::submit);
    connect(auth_, &AuthController::registrationSucceeded,
            this, &RegisterDialog::onSucceeded);
    connect(auth_, &AuthController::operationFailed,
            this, &RegisterDialog::onFailed);
    connect(auth_, &AuthController::busyChanged,
            this, &RegisterDialog::setBusy);
}

void RegisterDialog::submit()
{
    statusLabel_->clear();
    if (passwordEdit_->text() != confirmEdit_->text())
    {
        statusLabel_->setText(QStringLiteral("两次输入的密码不一致"));
        return;
    }
    auth_->registerUser(endpoint_, nameEdit_->text().trimmed(),
                        passwordEdit_->text());
}

void RegisterDialog::onSucceeded(int userId)
{
    if (!isVisible()) return;
    emit registrationComplete(userId);
    accept();
}

void RegisterDialog::onFailed(const QString &message)
{
    if (isVisible()) statusLabel_->setText(message);
}

void RegisterDialog::setBusy(bool busy)
{
    if (!isVisible()) return;
    submitButton_->setEnabled(!busy);
    submitButton_->setText(busy ? QStringLiteral("正在注册…")
                                : QStringLiteral("创建账号"));
}

} // namespace client
} // namespace nexusim
