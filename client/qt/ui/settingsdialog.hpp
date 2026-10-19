#ifndef NEXUSIM_QT_SETTINGSDIALOG_HPP
#define NEXUSIM_QT_SETTINGSDIALOG_HPP

#include "network/networkclient.hpp"

#include <QDialog>

class QLineEdit;
class QSpinBox;

namespace nexusim {
namespace client {

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(const ServerEndpoint &endpoint,
                            QWidget *parent = nullptr);
    ServerEndpoint endpoint() const;

private:
    QLineEdit *hostEdit_;
    QSpinBox *portSpin_;
    QSpinBox *connectTimeoutSpin_;
    QSpinBox *requestTimeoutSpin_;
};

} // namespace client
} // namespace nexusim

#endif
