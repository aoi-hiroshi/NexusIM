#ifndef NEXUSIM_QT_CHATWIDGET_HPP
#define NEXUSIM_QT_CHATWIDGET_HPP

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace nexusim {
namespace client {

class ChatController;
class ConversationModel;

class ChatWidget : public QWidget
{
    Q_OBJECT
public:
    ChatWidget(ChatController *controller, ConversationModel *model,
               QWidget *parent = nullptr);
    void setConversation(int userId, const QString &displayName);
    void clearConversation();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void sendCurrentMessage();
    void refreshIfActive(int userId);

private:
    void rebuildMessages();
    void clearMessageWidgets();

    ChatController *controller_;
    ConversationModel *model_;
    int userId_{-1};
    QString displayName_;
    QLabel *titleLabel_;
    QLabel *subtitleLabel_;
    QScrollArea *scrollArea_;
    QWidget *messageContainer_;
    QVBoxLayout *messageLayout_;
    QPlainTextEdit *input_;
    QPushButton *sendButton_;
};

} // namespace client
} // namespace nexusim

#endif
