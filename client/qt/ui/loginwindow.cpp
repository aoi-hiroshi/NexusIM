#include "ui/loginwindow.hpp"

#include "controllers/authcontroller.hpp"
#include "ui/registerdialog.hpp"
#include "ui/settingsdialog.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace nexusim {
namespace client {

LoginWindow::LoginWindow(AuthController *auth, NetworkClient *network,
                         const ServerEndpoint &endpoint, QWidget *parent)
    : QWidget(parent), auth_(auth), network_(network), endpoint_(endpoint),
      idEdit_(new QLineEdit(this)), passwordEdit_(new QLineEdit(this)),
      loginButton_(new QPushButton(QStringLiteral("登录"), this)),
      statusLabel_(new QLabel(this)), endpointLabel_(new QLabel(this)),
      networkLabel_(new QLabel(QStringLiteral("未连接"), this))
{
    setWindowTitle(QStringLiteral("NexusIM"));
    setMinimumSize(780, 560);
    resize(900, 620);

    auto *brand = new QLabel(QStringLiteral("N"), this);
    brand->setObjectName(QStringLiteral("brandMark"));
    brand->setAlignment(Qt::AlignCenter);
    auto *title = new QLabel(QStringLiteral("NexusIM"), this);
    title->setObjectName(QStringLiteral("loginTitle"));
    auto *subtitle = new QLabel(QStringLiteral("连接你的分布式即时通信空间"), this);
    subtitle->setObjectName(QStringLiteral("mutedLabel"));

    idEdit_->setPlaceholderText(QStringLiteral("用户 ID"));
    idEdit_->setValidator(new QIntValidator(1, 2147483647, idEdit_));
    passwordEdit_->setPlaceholderText(QStringLiteral("密码"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    passwordEdit_->setMaxLength(128);

    auto *showPassword = new QPushButton(QStringLiteral("显示密码"), this);
    showPassword->setCheckable(true);
    showPassword->setObjectName(QStringLiteral("linkButton"));
    connect(showPassword, &QPushButton::toggled, this,
            [this, showPassword](bool checked) {
                passwordEdit_->setEchoMode(checked ? QLineEdit::Normal
                                                   : QLineEdit::Password);
                showPassword->setText(checked ? QStringLiteral("隐藏密码")
                                              : QStringLiteral("显示密码"));
            });

    loginButton_->setObjectName(QStringLiteral("primaryButton"));
    loginButton_->setMinimumHeight(44);
    auto *registerButton = new QPushButton(QStringLiteral("注册新账号"), this);
    registerButton->setObjectName(QStringLiteral("secondaryButton"));
    registerButton->setMinimumHeight(42);
    auto *settingsButton = new QPushButton(QStringLiteral("连接设置"), this);
    settingsButton->setObjectName(QStringLiteral("linkButton"));

    statusLabel_->setObjectName(QStringLiteral("errorLabel"));
    statusLabel_->setWordWrap(true);
    endpointLabel_->setObjectName(QStringLiteral("mutedLabel"));
    endpointLabel_->setAlignment(Qt::AlignCenter);
    networkLabel_->setObjectName(QStringLiteral("statusPill"));

    auto *passwordRow = new QHBoxLayout;
    passwordRow->addWidget(passwordEdit_, 1);
    passwordRow->addWidget(showPassword);
    auto *endpointRow = new QHBoxLayout;
    endpointRow->addStretch();
    endpointRow->addWidget(networkLabel_);
    endpointRow->addWidget(settingsButton);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("loginCard"));
    card->setMaximumWidth(430);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(42, 38, 42, 36);
    cardLayout->setSpacing(15);
    cardLayout->addWidget(brand, 0, Qt::AlignCenter);
    cardLayout->addWidget(title, 0, Qt::AlignCenter);
    cardLayout->addWidget(subtitle, 0, Qt::AlignCenter);
    cardLayout->addSpacing(10);
    cardLayout->addWidget(idEdit_);
    cardLayout->addLayout(passwordRow);
    cardLayout->addWidget(statusLabel_);
    cardLayout->addWidget(loginButton_);
    cardLayout->addWidget(registerButton);
    cardLayout->addWidget(endpointLabel_);
    cardLayout->addLayout(endpointRow);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(40, 35, 40, 35);
    layout->addStretch();
    layout->addWidget(card, 0, Qt::AlignCenter);
    layout->addStretch();

    connect(loginButton_, &QPushButton::clicked,
            this, &LoginWindow::submitLogin);
    connect(passwordEdit_, &QLineEdit::returnPressed,
            this, &LoginWindow::submitLogin);
    connect(registerButton, &QPushButton::clicked,
            this, &LoginWindow::openRegister);
    connect(settingsButton, &QPushButton::clicked,
            this, &LoginWindow::openSettings);
    connect(auth_, &AuthController::busyChanged,
            this, &LoginWindow::setBusy);
    connect(auth_, &AuthController::operationFailed,
            this, &LoginWindow::showError);
    connect(network_, &NetworkClient::stateChanged,
            this, &LoginWindow::updateNetworkState);
    updateEndpointLabel();
}

ServerEndpoint LoginWindow::endpoint() const { return endpoint_; }

void LoginWindow::setEndpoint(const ServerEndpoint &endpoint)
{
    endpoint_ = endpoint;
    QSettings settings;
    settings.setValue(QStringLiteral("server/host"), endpoint_.host);
    settings.setValue(QStringLiteral("server/port"), endpoint_.port);
    settings.setValue(QStringLiteral("server/connectTimeoutMs"), endpoint_.connectTimeoutMs);
    settings.setValue(QStringLiteral("server/requestTimeoutMs"), endpoint_.requestTimeoutMs);
    updateEndpointLabel();
}

void LoginWindow::showDisconnectedMessage(const QString &message)
{
    statusLabel_->setText(message);
    show();
    raise();
    activateWindow();
}

void LoginWindow::submitLogin()
{
    statusLabel_->clear();
    auth_->login(endpoint_, idEdit_->text().toInt(), passwordEdit_->text());
}

void LoginWindow::openRegister()
{
    RegisterDialog dialog(auth_, endpoint_, this);
    connect(&dialog, &RegisterDialog::registrationComplete, this,
            [this](int id) {
                idEdit_->setText(QString::number(id));
                passwordEdit_->clear();
                statusLabel_->setText(
                    QStringLiteral("注册成功，用户 ID 为 %1，请妥善保存并登录。").arg(id));
            });
    dialog.exec();
}

void LoginWindow::openSettings()
{
    SettingsDialog dialog(endpoint_, this);
    if (dialog.exec() == QDialog::Accepted) setEndpoint(dialog.endpoint());
}

void LoginWindow::setBusy(bool busy)
{
    loginButton_->setEnabled(!busy);
    loginButton_->setText(busy ? QStringLiteral("正在连接并登录…")
                               : QStringLiteral("登录"));
}

void LoginWindow::showError(const QString &message)
{
    if (isVisible()) statusLabel_->setText(message);
}

void LoginWindow::updateNetworkState(NetworkClient::State state)
{
    switch (state)
    {
    case NetworkClient::State::Connected:
        networkLabel_->setText(QStringLiteral("已连接")); break;
    case NetworkClient::State::Connecting:
        networkLabel_->setText(QStringLiteral("连接中")); break;
    case NetworkClient::State::Disconnected:
        networkLabel_->setText(QStringLiteral("未连接")); break;
    }
}

void LoginWindow::updateEndpointLabel()
{
    endpointLabel_->setText(QStringLiteral("当前入口  %1:%2")
                                .arg(endpoint_.host)
                                .arg(endpoint_.port));
}

} // namespace client
} // namespace nexusim
