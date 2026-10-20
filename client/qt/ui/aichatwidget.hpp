#ifndef NEXUSIM_QT_AICHATWIDGET_HPP
#define NEXUSIM_QT_AICHATWIDGET_HPP

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QTimer;
class QVBoxLayout;

namespace nexusim {
namespace client {

class AIController;

class AIChatWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AIChatWidget(AIController *controller,
                          QWidget *parent = nullptr);
    void reset();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void sendCurrentPrompt();
    void appendAcceptedPrompt(const QString &requestId,
                              const QString &prompt);
    void appendReply(const QString &requestId, const QString &content,
                     const QString &model, const QString &finishReason,
                     qint64 elapsedMs, const QString &warning);
    void appendFailure(const QString &requestId, const QString &code,
                       const QString &message);
    void setBusy(bool busy);
    void updateWaitingText();
    void startNewConversation();
    void clearLocalDisplay();

private:
    enum class BubbleKind { User, Assistant, Error, System };
    void appendBubble(const QString &text, BubbleKind kind,
                      const QString &meta = QString());
    void clearMessageWidgets();
    void scrollToBottom();

    AIController *controller_;
    QScrollArea *scrollArea_;
    QWidget *messageContainer_;
    QVBoxLayout *messageLayout_;
    QPlainTextEdit *input_;
    QPushButton *sendButton_;
    QLabel *waitingLabel_;
    QTimer *waitingTimer_;
    int waitingStep_{0};
};

} // namespace client
} // namespace nexusim

#endif
