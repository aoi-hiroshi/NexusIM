#ifndef NEXUSIM_QT_GROUPCHATWIDGET_HPP
#define NEXUSIM_QT_GROUPCHATWIDGET_HPP

#include <QJsonArray>
#include <QWidget>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace nexusim {
namespace client {

class SocialController;

class GroupChatWidget : public QWidget
{
    Q_OBJECT
public:
    explicit GroupChatWidget(SocialController *controller,
                             QWidget *parent = nullptr);
    void setGroup(int groupId, const QString &name, const QString &description,
                  const QString &role);
    void clearGroup();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void sendCurrentMessage();
    void refreshMessages(int groupId);
    void showMembers(int groupId, const QJsonArray &members);

private:
    void rebuildMessages();
    void clearMessageWidgets();

    SocialController *controller_;
    int groupId_{-1};
    QLabel *titleLabel_;
    QLabel *subtitleLabel_;
    QListWidget *members_;
    QScrollArea *scrollArea_;
    QWidget *messageContainer_;
    QVBoxLayout *messageLayout_;
    QPlainTextEdit *input_;
    QPushButton *sendButton_;
};

} // namespace client
} // namespace nexusim

#endif
