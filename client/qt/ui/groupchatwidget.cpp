#include "ui/groupchatwidget.hpp"

#include "controllers/socialcontroller.hpp"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace nexusim {
namespace client {

GroupChatWidget::GroupChatWidget(SocialController *controller, QWidget *parent)
    : QWidget(parent), controller_(controller),
      titleLabel_(new QLabel(QStringLiteral("选择一个群组"), this)),
      subtitleLabel_(new QLabel(QStringLiteral("群成员由服务端权限校验"), this)),
      members_(new QListWidget(this)), scrollArea_(new QScrollArea(this)),
      messageContainer_(new QWidget), messageLayout_(new QVBoxLayout),
      input_(new QPlainTextEdit(this)),
      sendButton_(new QPushButton(QStringLiteral("发送"), this))
{
    setObjectName(QStringLiteral("chatPanel"));
    setProperty("stage6Role", QStringLiteral("groupChatWidget"));
    titleLabel_->setObjectName(QStringLiteral("chatTitle"));
    subtitleLabel_->setObjectName(QStringLiteral("mutedLabel"));
    auto *heading = new QVBoxLayout;
    heading->setContentsMargins(22, 12, 8, 10);
    heading->setSpacing(2);
    heading->addWidget(titleLabel_);
    heading->addWidget(subtitleLabel_);
    members_->setMaximumWidth(210);
    members_->setToolTip(QStringLiteral("群成员"));
    auto *headerLayout = new QHBoxLayout;
    headerLayout->setContentsMargins(0, 0, 10, 0);
    headerLayout->addLayout(heading, 1);
    headerLayout->addWidget(members_);
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

    input_->setPlaceholderText(QStringLiteral("输入群消息；Enter 发送，Ctrl+Enter 换行"));
    input_->setMinimumHeight(82);
    input_->setMaximumHeight(130);
    input_->installEventFilter(this);
    sendButton_->setObjectName(QStringLiteral("primaryButton"));
    sendButton_->setFixedSize(88, 38);
    auto *inputLayout = new QHBoxLayout;
    inputLayout->setContentsMargins(18, 12, 18, 16);
    inputLayout->addWidget(input_, 1);
    inputLayout->addWidget(sendButton_, 0, Qt::AlignBottom);
    auto *inputPanel = new QWidget(this);
    inputPanel->setObjectName(QStringLiteral("inputPanel"));
    inputPanel->setLayout(inputLayout);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(scrollArea_, 1);
    layout->addWidget(inputPanel);

    connect(sendButton_, &QPushButton::clicked,
            this, &GroupChatWidget::sendCurrentMessage);
    connect(controller_, &SocialController::groupMessagesChanged,
            this, &GroupChatWidget::refreshMessages);
    connect(controller_, &SocialController::groupMembersChanged,
            this, &GroupChatWidget::showMembers);
    clearGroup();
}

void GroupChatWidget::setGroup(int groupId, const QString &name,
                               const QString &description, const QString &role)
{
    groupId_ = groupId;
    titleLabel_->setText(QStringLiteral("%1 (#%2)").arg(name).arg(groupId));
    subtitleLabel_->setText(QStringLiteral("%1 · 我的角色：%2")
                                .arg(description.isEmpty() ? QStringLiteral("暂无群描述")
                                                           : description,
                                     role.isEmpty() ? QStringLiteral("成员") : role));
    input_->setEnabled(true);
    sendButton_->setEnabled(true);
    controller_->openGroup(groupId_);
    rebuildMessages();
}

void GroupChatWidget::clearGroup()
{
    groupId_ = -1;
    titleLabel_->setText(QStringLiteral("选择一个群组"));
    subtitleLabel_->setText(QStringLiteral("群成员与消息均来自真实服务端数据"));
    members_->clear();
    input_->clear();
    input_->setEnabled(false);
    sendButton_->setEnabled(false);
    clearMessageWidgets();
}

bool GroupChatWidget::eventFilter(QObject *watched, QEvent *event)
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

void GroupChatWidget::sendCurrentMessage()
{
    if (controller_->sendGroupMessage(groupId_, input_->toPlainText()))
        input_->clear();
}

void GroupChatWidget::refreshMessages(int groupId)
{
    if (groupId == groupId_) rebuildMessages();
}

void GroupChatWidget::showMembers(int groupId, const QJsonArray &members)
{
    if (groupId != groupId_) return;
    members_->clear();
    for (const QJsonValue &value : members)
    {
        const QJsonObject member = value.toObject();
        members_->addItem(QStringLiteral("%1 (#%2) · %3")
                              .arg(member.value(QStringLiteral("name")).toString())
                              .arg(member.value(QStringLiteral("id")).toInt())
                              .arg(member.value(QStringLiteral("role")).toString()));
    }
}

void GroupChatWidget::rebuildMessages()
{
    clearMessageWidgets();
    if (groupId_ <= 0) return;
    const auto &values = controller_->messages(groupId_);
    for (const GroupChatMessage &message : values)
    {
        auto *bubble = new QFrame(messageContainer_);
        bubble->setObjectName(QStringLiteral("messageBubble"));
        bubble->setProperty("outgoing", message.outgoing);
        bubble->setProperty("system", message.system);
        auto *sender = new QLabel(
            message.outgoing ? QStringLiteral("我") : message.senderName, bubble);
        sender->setObjectName(QStringLiteral("mutedLabel"));
        auto *text = new QLabel(message.text, bubble);
        text->setWordWrap(true);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto *time = new QLabel(
            QStringLiteral("%1%2")
                .arg(message.offline ? QStringLiteral("离线消息 · ") : QString())
                .arg(message.timestamp.toString(QStringLiteral("MM-dd HH:mm:ss"))), bubble);
        time->setObjectName(QStringLiteral("mutedLabel"));
        auto *bubbleLayout = new QVBoxLayout(bubble);
        bubbleLayout->setContentsMargins(12, 8, 12, 8);
        bubbleLayout->addWidget(sender);
        bubbleLayout->addWidget(text);
        bubbleLayout->addWidget(time);
        auto *row = new QHBoxLayout;
        if (message.outgoing) row->addStretch();
        row->addWidget(bubble, 0, message.outgoing ? Qt::AlignRight : Qt::AlignLeft);
        if (!message.outgoing) row->addStretch();
        messageLayout_->insertLayout(messageLayout_->count() - 1, row);
    }
    QTimer::singleShot(0, this, [this] {
        scrollArea_->verticalScrollBar()->setValue(
            scrollArea_->verticalScrollBar()->maximum());
    });
}

void GroupChatWidget::clearMessageWidgets()
{
    while (messageLayout_->count() > 1)
    {
        QLayoutItem *item = messageLayout_->takeAt(0);
        if (item->layout())
        {
            while (QLayoutItem *child = item->layout()->takeAt(0))
            {
                delete child->widget();
                delete child;
            }
            delete item->layout();
        }
        else delete item->widget();
        delete item;
    }
}

} // namespace client
} // namespace nexusim
