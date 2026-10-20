#include "ui/aichatwidget.hpp"

#include "controllers/aicontroller.hpp"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace nexusim {
namespace client {

AIChatWidget::AIChatWidget(AIController *controller, QWidget *parent)
    : QWidget(parent), controller_(controller),
      scrollArea_(new QScrollArea(this)), messageContainer_(new QWidget),
      messageLayout_(new QVBoxLayout), input_(new QPlainTextEdit(this)),
      sendButton_(new QPushButton(QStringLiteral("发送"), this)),
      waitingLabel_(new QLabel(this)), waitingTimer_(new QTimer(this))
{
    setObjectName(QStringLiteral("chatPanel"));
    auto *mark = new QLabel(QStringLiteral("AI"), this);
    mark->setObjectName(QStringLiteral("aiAvatar"));
    mark->setAlignment(Qt::AlignCenter);
    mark->setFixedSize(42, 42);
    auto *title = new QLabel(QStringLiteral("DeepSeek AI 助手"), this);
    title->setObjectName(QStringLiteral("chatTitle"));
    auto *subtitle = new QLabel(
        QStringLiteral("DeepSeek V4 Pro · 非流式文本回复 · 服务端上下文"), this);
    subtitle->setObjectName(QStringLiteral("mutedLabel"));
    auto *titleColumn = new QVBoxLayout;
    titleColumn->setContentsMargins(0, 0, 0, 0);
    titleColumn->setSpacing(2);
    titleColumn->addWidget(title);
    titleColumn->addWidget(subtitle);
    auto *newButton = new QPushButton(QStringLiteral("新对话"), this);
    newButton->setObjectName(QStringLiteral("secondaryButton"));
    auto *clearButton = new QPushButton(QStringLiteral("清空显示"), this);
    clearButton->setObjectName(QStringLiteral("linkButton"));
    clearButton->setToolTip(
        QStringLiteral("只清空本地界面，不删除 AIService 中的当前上下文"));
    auto *headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(22, 12, 18, 12);
    headerLayout->setSpacing(12);
    headerLayout->addWidget(mark);
    headerLayout->addLayout(titleColumn, 1);
    headerLayout->addWidget(clearButton);
    headerLayout->addWidget(newButton);
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("chatHeader"));
    header->setLayout(headerLayout);

    messageLayout_->setContentsMargins(22, 18, 22, 18);
    messageLayout_->setSpacing(10);
    messageLayout_->addStretch();
    messageContainer_->setLayout(messageLayout_);
    scrollArea_->setWidget(messageContainer_);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    waitingLabel_->setObjectName(QStringLiteral("aiWaitingLabel"));
    waitingLabel_->setText(QStringLiteral("DeepSeek 正在思考"));
    waitingLabel_->hide();
    waitingTimer_->setInterval(400);

    input_->setPlaceholderText(
        QStringLiteral("输入问题；Enter 发送，Ctrl+Enter 换行"));
    input_->setMaximumHeight(150);
    input_->setMinimumHeight(88);
    input_->installEventFilter(this);
    sendButton_->setObjectName(QStringLiteral("primaryButton"));
    sendButton_->setFixedSize(88, 38);
    auto *inputColumn = new QVBoxLayout;
    inputColumn->setContentsMargins(0, 0, 0, 0);
    inputColumn->setSpacing(4);
    inputColumn->addWidget(waitingLabel_);
    inputColumn->addWidget(input_);
    auto *inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(18, 10, 18, 16);
    inputRow->addLayout(inputColumn, 1);
    inputRow->addWidget(sendButton_, 0, Qt::AlignBottom);
    auto *inputPanel = new QWidget(this);
    inputPanel->setObjectName(QStringLiteral("inputPanel"));
    inputPanel->setLayout(inputRow);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(scrollArea_, 1);
    layout->addWidget(inputPanel);

    connect(sendButton_, &QPushButton::clicked,
            this, &AIChatWidget::sendCurrentPrompt);
    connect(newButton, &QPushButton::clicked,
            this, &AIChatWidget::startNewConversation);
    connect(clearButton, &QPushButton::clicked,
            this, &AIChatWidget::clearLocalDisplay);
    connect(waitingTimer_, &QTimer::timeout,
            this, &AIChatWidget::updateWaitingText);
    connect(controller_, &AIController::promptAccepted,
            this, &AIChatWidget::appendAcceptedPrompt);
    connect(controller_, &AIController::replyReceived,
            this, &AIChatWidget::appendReply);
    connect(controller_, &AIController::requestFailed,
            this, &AIChatWidget::appendFailure);
    connect(controller_, &AIController::busyChanged,
            this, &AIChatWidget::setBusy);

    appendBubble(QStringLiteral(
        "你好，我是 NexusIM 的 DeepSeek AI 助手。\n"
        "对话经 ChatServer 和独立 AIService 处理，不会由客户端直连模型 API。"),
        BubbleKind::System);
}

void AIChatWidget::reset()
{
    input_->clear();
    setBusy(false);
    clearMessageWidgets();
    appendBubble(QStringLiteral("登录后可以开始新的 AI 对话。"),
                 BubbleKind::System);
}

bool AIChatWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == input_ && event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            !(key->modifiers() & Qt::ControlModifier))
        {
            sendCurrentPrompt();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AIChatWidget::sendCurrentPrompt()
{
    const QString prompt = input_->toPlainText();
    if (prompt.trimmed().isEmpty()) return;
    controller_->sendPrompt(prompt);
}

void AIChatWidget::appendAcceptedPrompt(const QString &, const QString &prompt)
{
    input_->clear();
    appendBubble(prompt, BubbleKind::User);
}

void AIChatWidget::appendReply(const QString &, const QString &content,
                               const QString &model,
                               const QString &finishReason,
                               qint64 elapsedMs, const QString &warning)
{
    QString meta = QStringLiteral("%1 · %2 s")
                       .arg(model.isEmpty() ? QStringLiteral("DeepSeek") : model)
                       .arg(elapsedMs / 1000.0, 0, 'f', 1);
    QString displayed = content;
    if (finishReason == QStringLiteral("length") || !warning.isEmpty())
    {
        const QString notice = warning.isEmpty()
                                   ? QStringLiteral("回复可能因长度限制而截断")
                                   : warning;
        displayed += QStringLiteral("\n\n⚠ %1").arg(notice);
    }
    appendBubble(displayed, BubbleKind::Assistant, meta);
}

void AIChatWidget::appendFailure(const QString &, const QString &code,
                                 const QString &message)
{
    appendBubble(message.isEmpty() ? QStringLiteral("AI 请求失败") : message,
                 BubbleKind::Error,
                 code.isEmpty() ? QStringLiteral("AI_ERROR") : code);
}

void AIChatWidget::setBusy(bool busy)
{
    sendButton_->setEnabled(!busy);
    input_->setEnabled(!busy);
    waitingStep_ = 0;
    waitingLabel_->setVisible(busy);
    if (busy)
    {
        waitingTimer_->start();
        updateWaitingText();
    }
    else
    {
        waitingTimer_->stop();
        waitingLabel_->setText(QStringLiteral("DeepSeek 正在思考"));
        input_->setFocus();
    }
}

void AIChatWidget::updateWaitingText()
{
    const QString dots((waitingStep_++ % 3) + 1, QLatin1Char('.'));
    waitingLabel_->setText(QStringLiteral("DeepSeek 正在思考%1").arg(dots));
}

void AIChatWidget::startNewConversation()
{
    controller_->newConversation();
    clearMessageWidgets();
    appendBubble(QStringLiteral("已开始新对话，后续问题不会携带上一个会话的上下文。"),
                 BubbleKind::System);
}

void AIChatWidget::clearLocalDisplay()
{
    clearMessageWidgets();
    appendBubble(QStringLiteral(
        "本地显示已清空；AIService 中的当前对话上下文仍保留。"
        "若要清除上下文，请点击“新对话”。"), BubbleKind::System);
}

void AIChatWidget::appendBubble(const QString &text, BubbleKind kind,
                                const QString &meta)
{
    auto *row = new QWidget(messageContainer_);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 2, 0, 2);
    auto *bubble = new QFrame(row);
    bubble->setObjectName(QStringLiteral("messageBubble"));
    bubble->setProperty("outgoing", kind == BubbleKind::User);
    bubble->setProperty("system", kind == BubbleKind::System);
    bubble->setProperty("error", kind == BubbleKind::Error);
    bubble->setProperty("assistant", kind == BubbleKind::Assistant);
    bubble->setMaximumWidth(650);
    auto *body = new QLabel(text, bubble);
    body->setWordWrap(true);
    body->setTextFormat(Qt::PlainText);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *bubbleLayout = new QVBoxLayout(bubble);
    bubbleLayout->setContentsMargins(13, 9, 13, 8);
    bubbleLayout->setSpacing(4);
    bubbleLayout->addWidget(body);
    if (!meta.isEmpty())
    {
        auto *metaLabel = new QLabel(meta, bubble);
        metaLabel->setObjectName(QStringLiteral("messageMeta"));
        bubbleLayout->addWidget(metaLabel, 0,
                                kind == BubbleKind::User ? Qt::AlignRight
                                                         : Qt::AlignLeft);
    }
    if (kind == BubbleKind::User)
    {
        rowLayout->addStretch();
        rowLayout->addWidget(bubble);
    }
    else if (kind == BubbleKind::System)
    {
        rowLayout->addStretch();
        rowLayout->addWidget(bubble);
        rowLayout->addStretch();
    }
    else
    {
        rowLayout->addWidget(bubble);
        rowLayout->addStretch();
    }
    messageLayout_->insertWidget(messageLayout_->count() - 1, row);
    scrollToBottom();
}

void AIChatWidget::clearMessageWidgets()
{
    while (messageLayout_->count() > 1)
    {
        QLayoutItem *item = messageLayout_->takeAt(0);
        delete item->widget();
        delete item;
    }
}

void AIChatWidget::scrollToBottom()
{
    QScrollBar *bar = scrollArea_->verticalScrollBar();
    QTimer::singleShot(0, this, [bar] { bar->setValue(bar->maximum()); });
}

} // namespace client
} // namespace nexusim
