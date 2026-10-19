#include "ui/settingsdialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace nexusim {
namespace client {

SettingsDialog::SettingsDialog(const ServerEndpoint &endpoint, QWidget *parent)
    : QDialog(parent), hostEdit_(new QLineEdit(endpoint.host, this)),
      portSpin_(new QSpinBox(this)),
      connectTimeoutSpin_(new QSpinBox(this)),
      requestTimeoutSpin_(new QSpinBox(this))
{
    setWindowTitle(QStringLiteral("连接设置"));
    setModal(true);
    setMinimumWidth(420);
    portSpin_->setRange(1, 65535);
    portSpin_->setValue(endpoint.port);
    connectTimeoutSpin_->setRange(500, 30000);
    connectTimeoutSpin_->setSingleStep(500);
    connectTimeoutSpin_->setSuffix(QStringLiteral(" ms"));
    connectTimeoutSpin_->setValue(endpoint.connectTimeoutMs);
    requestTimeoutSpin_->setRange(500, 30000);
    requestTimeoutSpin_->setSingleStep(500);
    requestTimeoutSpin_->setSuffix(QStringLiteral(" ms"));
    requestTimeoutSpin_->setValue(endpoint.requestTimeoutMs);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("服务器 Host"), hostEdit_);
    form->addRow(QStringLiteral("TCP 端口"), portSpin_);
    form->addRow(QStringLiteral("连接超时"), connectTimeoutSpin_);
    form->addRow(QStringLiteral("请求超时"), requestTimeoutSpin_);

    auto *hint = new QLabel(
        QStringLiteral("可填写 ChatServer-1、ChatServer-2 或 Nginx Stream 地址。"
                       "设置不会写入源码。"), this);
    hint->setWordWrap(true);
    hint->setObjectName(QStringLiteral("mutedLabel"));

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(hint);
    layout->addWidget(buttons);
}

ServerEndpoint SettingsDialog::endpoint() const
{
    ServerEndpoint result;
    result.host = hostEdit_->text().trimmed();
    result.port = static_cast<quint16>(portSpin_->value());
    result.connectTimeoutMs = connectTimeoutSpin_->value();
    result.requestTimeoutMs = requestTimeoutSpin_->value();
    return result;
}

} // namespace client
} // namespace nexusim
