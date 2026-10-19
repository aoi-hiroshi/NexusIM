#include "ui/chatwidget.hpp"

#include "controllers/chatcontroller.hpp"
#include "models/conversationmodel.hpp"

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

ChatWidget::ChatWidget(ChatController *controller, ConversationModel *model,
                       QWidget *parent)
    : QWidget(parent), controller_(controller), model_(model),
      titleLabel_(new QLabel(QStringLiteral("选择一个会话"), this)),
      subtitleLabel_(new QLabel(QStringLiteral("按用户 ID 发起真实单聊"), this)),
      scrollArea_(new QScrollArea(this)),
      messageContainer_(new QWidget), messageLayout_(new QVBoxLayout),
      input_(new QPlainTextEdit(this)),
      sendButton_(new QPushButton(QStringLiteral("发送"), this))
{
    setObjectName(QStringLiteral("chatPanel"));
    titleLabel_->setObjectName(QStringLiteral("chatTitle"));
    subtitleLabel_->setObjectName(QStringLiteral("mutedLabel"));
    auto *headerLayout = new QVBoxLayout;
    headerLayout->setContentsMargins(22, 14, 22, 12);
    headerLayout->setSpacing(2);
    headerLayout->addWidget(titleLabel_);
    headerLayout->addWidget(subtitleLabel_);
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

    input_->setPlaceholderText(QStringLiteral("输入消息；Enter 发送，Ctrl+Enter 换行"));
    input_->setMaximumHeight(130);
    input_->setMinimumHeight(82);
    input_->installEventFilter(this);
    sendButton_->setObjectName(QStringLiteral("primaryButton"));
    sendButton_->setFixedSize(88, 38);
    auto *inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(18, 12, 18, 16);
    inputRow->addWidget(input_, 1);
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
    input_->setEnabled(false);
    sendButton_->setEnabled(false);

    connect(sendButton_, &QPushButton::clicked,
            this, &ChatWidget::sendCurrentMessage);
    connect(model_, &ConversationModel::conversationUpdated,
            this, &ChatWidget::refreshIfActive);
}

void ChatWidget::setConversation(int userId, const QString &displayName)
{
    userId_ = userId;
    displayName_ = displayName;
    titleLabel_->setText(displayName_);
    subtitleLabel_->setText(
        QStringLiteral("用户 ID %1 · V1 不提供送达或已读回执").arg(userId_));
    input_->setEnabled(true);
    sendButton_->setEnabled(true);
    rebuildMessages();
    input_->setFocus();
}

void ChatWidget::clearConversation()
{
    userId_ = -1;
    displayName_.clear();
    titleLabel_->setText(QStringLiteral("选择一个会话"));
    subtitleLabel_->setText(QStringLiteral("按用户 ID 发起真实单聊"));
    input_->clear();
    input_->setEnabled(false);
    sendButton_->setEnabled(false);
    clearMessageWidgets();
}

bool ChatWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == input_ && event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            !(key->modifiers() & Qt::ControlModifier))
        {
            sendCurrentMessage();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ChatWidget::sendCurrentMessage()
{
    if (userId_ <= 0) return;
    if (controller_->sendMessage(userId_, displayName_, input_->toPlainText()))
        input_->clear();
}

void ChatWidget::refreshIfActive(int userId)
{
    if (userId == userId_) rebuildMessages();
}

void ChatWidget::rebuildMessages()
{
    QScrollBar *bar = scrollArea_->verticalScrollBar();
    const bool followBottom = bar->maximum() - bar->value() <= 40;
    clearMessageWidgets();
    const Conversation *conversation = model_->conversation(userId_);
    if (!conversation) return;
    if (!conversation->displayName.isEmpty() &&
        conversation->displayName != displayName_)
    {
        displayName_ = conversation->displayName;
        titleLabel_->setText(displayName_);
    }
    for (const ChatMessage &message : conversation->messages)
    {
        auto *row = new QWidget(messageContainer_);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 2, 0, 2);
        auto *bubble = new QFrame(row);
        bubble->setProperty("outgoing", message.outgoing);
        bubble->setProperty("system", message.system);
        bubble->setObjectName(QStringLiteral("messageBubble"));
        bubble->setMaximumWidth(560);
        auto *text = new QLabel(message.text, bubble);
        text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto *meta = new QLabel(
            QStringLiteral("%1%2")
                .arg(message.timestamp.toString(QStringLiteral("HH:mm")),
                     message.offline ? QStringLiteral(" · 离线消息") : QString()),
            bubble);
        meta->setObjectName(QStringLiteral("messageMeta"));
        auto *bubbleLayout = new QVBoxLayout(bubble);
        bubbleLayout->setContentsMargins(13, 9, 13, 8);
        bubbleLayout->setSpacing(4);
        bubbleLayout->addWidget(text);
        bubbleLayout->addWidget(meta, 0,
                                message.outgoing ? Qt::AlignRight : Qt::AlignLeft);
        if (message.system)
        {
            rowLayout->addStretch();
            rowLayout->addWidget(bubble);
            rowLayout->addStretch();
        }
        else if (message.outgoing)
        {
            rowLayout->addStretch();
            rowLayout->addWidget(bubble);
        }
        else
        {
            rowLayout->addWidget(bubble);
            rowLayout->addStretch();
        }
        messageLayout_->insertWidget(messageLayout_->count() - 1, row);
    }
    if (followBottom)
        QTimer::singleShot(0, this, [bar] { bar->setValue(bar->maximum()); });
}

void ChatWidget::clearMessageWidgets()
{
    while (messageLayout_->count() > 1)
    {
        QLayoutItem *item = messageLayout_->takeAt(0);
        delete item->widget();
        delete item;
    }
}

} // namespace client
} // namespace nexusim
