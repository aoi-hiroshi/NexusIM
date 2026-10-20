#include "ui/mainwindow.hpp"

#include "controllers/chatcontroller.hpp"
#include "controllers/socialcontroller.hpp"
#include "controllers/aicontroller.hpp"
#include "ui/aichatwidget.hpp"
#include "ui/chatwidget.hpp"
#include "ui/conversationlist.hpp"
#include "ui/groupchatwidget.hpp"
#include "ui/settingsdialog.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>

namespace nexusim {
namespace client {

MainWindow::MainWindow(ChatController *chat, SocialController *social,
                       AIController *ai,
                       NetworkClient *network,
                       QWidget *parent)
    : QMainWindow(parent), chat_(chat), social_(social), ai_(ai), network_(network),
      avatarLabel_(new QLabel(QStringLiteral("N"), this)),
      connectionLabel_(new QLabel(QStringLiteral("未连接"), this)),
      middleStack_(new QStackedWidget(this)), rightStack_(new QStackedWidget(this)),
      conversationList_(new ConversationList(chat_->conversations(), this)),
      chatWidget_(new ChatWidget(chat_, chat_->conversations(), this)),
      aiChatWidget_(new AIChatWidget(ai_, this)),
      groupChatWidget_(new GroupChatWidget(social_, this)),
      contactList_(new QListWidget(this)), groupList_(new QListWidget(this)),
      contactSearch_(new QLineEdit(this))
{
    setWindowTitle(QStringLiteral("NexusIM"));
    setMinimumSize(1050, 680);
    resize(1260, 820);

    auto *nav = new QWidget(this);
    nav->setObjectName(QStringLiteral("navPanel"));
    nav->setFixedWidth(72);
    avatarLabel_->setObjectName(QStringLiteral("navAvatar"));
    avatarLabel_->setAlignment(Qt::AlignCenter);
    avatarLabel_->setFixedSize(44, 44);
    messagesButton_ = makeNavButton(QStringLiteral("消"), QStringLiteral("消息"));
    contactsButton_ = makeNavButton(QStringLiteral("联"), QStringLiteral("联系人"));
    groupsButton_ = makeNavButton(QStringLiteral("群"), QStringLiteral("群组"));
    aiButton_ = makeNavButton(QStringLiteral("AI"), QStringLiteral("AI 助手"));
    auto *settings = makeNavButton(QStringLiteral("设"), QStringLiteral("连接设置"));
    auto *logout = makeNavButton(QStringLiteral("退"), QStringLiteral("退出登录"));
    auto *navLayout = new QVBoxLayout(nav);
    navLayout->setContentsMargins(14, 18, 14, 16);
    navLayout->setSpacing(10);
    navLayout->addWidget(avatarLabel_, 0, Qt::AlignCenter);
    navLayout->addSpacing(14);
    navLayout->addWidget(messagesButton_);
    navLayout->addWidget(contactsButton_);
    navLayout->addWidget(groupsButton_);
    navLayout->addWidget(aiButton_);
    navLayout->addStretch();
    navLayout->addWidget(settings);
    navLayout->addWidget(logout);

    middleStack_->setMinimumWidth(270);
    middleStack_->setMaximumWidth(330);
    middleStack_->addWidget(conversationList_);

    auto *contactPanel = new QWidget(this);
    contactPanel->setObjectName(QStringLiteral("middlePanel"));
    auto *contactTitle = new QLabel(QStringLiteral("联系人"), contactPanel);
    contactTitle->setObjectName(QStringLiteral("panelTitle"));
    auto *contactHint = new QLabel(
        QStringLiteral("好友关系由服务端双向保存；在线状态暂不展示。"), contactPanel);
    contactHint->setWordWrap(true);
    contactHint->setObjectName(QStringLiteral("mutedLabel"));
    contactSearch_->setPlaceholderText(QStringLiteral("按姓名或用户 ID 筛选"));
    auto *addFriendButton = new QPushButton(QStringLiteral("添加好友"), contactPanel);
    addFriendButton->setObjectName(QStringLiteral("secondaryButton"));
    auto *refreshFriendButton = new QPushButton(QStringLiteral("刷新"), contactPanel);
    auto *contactActions = new QHBoxLayout;
    contactActions->addWidget(addFriendButton);
    contactActions->addWidget(refreshFriendButton);
    contactList_->setFrameShape(QFrame::NoFrame);
    auto *contactLayout = new QVBoxLayout(contactPanel);
    contactLayout->setContentsMargins(14, 18, 14, 12);
    contactLayout->addWidget(contactTitle);
    contactLayout->addWidget(contactHint);
    contactLayout->addWidget(contactSearch_);
    contactLayout->addLayout(contactActions);
    contactLayout->addWidget(contactList_, 1);
    middleStack_->addWidget(contactPanel);

    auto *groupPanel = new QWidget(this);
    groupPanel->setObjectName(QStringLiteral("middlePanel"));
    auto *groupTitle = new QLabel(QStringLiteral("群组"), groupPanel);
    groupTitle->setObjectName(QStringLiteral("panelTitle"));
    auto *groupHint = new QLabel(QStringLiteral("双击群组进入聊天；公开群可按 ID 加入。"),
                                 groupPanel);
    groupHint->setWordWrap(true);
    groupHint->setObjectName(QStringLiteral("mutedLabel"));
    auto *createGroupButton = new QPushButton(QStringLiteral("创建"), groupPanel);
    createGroupButton->setObjectName(QStringLiteral("secondaryButton"));
    auto *joinGroupButton = new QPushButton(QStringLiteral("加入"), groupPanel);
    auto *leaveGroupButton = new QPushButton(QStringLiteral("退出"), groupPanel);
    auto *refreshGroupButton = new QPushButton(QStringLiteral("刷新"), groupPanel);
    auto *groupActions = new QHBoxLayout;
    groupActions->addWidget(createGroupButton);
    groupActions->addWidget(joinGroupButton);
    groupActions->addWidget(leaveGroupButton);
    groupActions->addWidget(refreshGroupButton);
    groupList_->setFrameShape(QFrame::NoFrame);
    auto *groupLayout = new QVBoxLayout(groupPanel);
    groupLayout->setContentsMargins(14, 18, 14, 12);
    groupLayout->addWidget(groupTitle);
    groupLayout->addWidget(groupHint);
    groupLayout->addLayout(groupActions);
    groupLayout->addWidget(groupList_, 1);
    middleStack_->addWidget(groupPanel);
    middleStack_->addWidget(makeUnavailablePage(
        QStringLiteral("AI 助手"),
        QStringLiteral("DeepSeek V4 Pro 由独立 AIService 调用；对话上下文在服务重启后丢失。")));

    rightStack_->addWidget(chatWidget_);
    rightStack_->addWidget(makeUnavailablePage(
        QStringLiteral("联系人"), QStringLiteral("双击左侧联系人或输入用户 ID 发起会话。")));
    rightStack_->addWidget(groupChatWidget_);
    rightStack_->addWidget(aiChatWidget_);

    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(nav);
    layout->addWidget(middleStack_);
    layout->addWidget(rightStack_, 1);
    setCentralWidget(central);

    connectionLabel_->setObjectName(QStringLiteral("statusPill"));
    statusBar()->addPermanentWidget(connectionLabel_);
    statusBar()->showMessage(QStringLiteral("V1 单聊不提供送达或已读回执"));

    connect(messagesButton_, &QPushButton::clicked,
            this, &MainWindow::showMessagesPage);
    connect(contactsButton_, &QPushButton::clicked,
            this, &MainWindow::showContactsPage);
    connect(groupsButton_, &QPushButton::clicked,
            this, &MainWindow::showGroupsPage);
    connect(aiButton_, &QPushButton::clicked,
            this, &MainWindow::showAiPage);
    connect(settings, &QPushButton::clicked,
            this, &MainWindow::openSettings);
    connect(logout, &QPushButton::clicked,
            this, &MainWindow::logoutRequested);
    connect(conversationList_, &ConversationList::startConversationRequested,
            this, &MainWindow::startConversation);
    connect(conversationList_, &ConversationList::conversationSelected,
            this, &MainWindow::selectConversation);
    connect(contactList_, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *) { openContactConversation(); });
    connect(groupList_, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *) { openGroupConversation(); });
    connect(contactSearch_, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int row = 0; row < contactList_->count(); ++row)
            contactList_->item(row)->setHidden(
                !contactList_->item(row)->text().contains(text, Qt::CaseInsensitive));
    });
    connect(addFriendButton, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const int id = QInputDialog::getInt(this, QStringLiteral("添加好友"),
                                            QStringLiteral("目标用户 ID"), 1, 1,
                                            2147483647, 1, &ok);
        if (ok) social_->addFriend(id);
    });
    connect(refreshFriendButton, &QPushButton::clicked,
            social_, &SocialController::refreshFriends);
    connect(createGroupButton, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, QStringLiteral("创建群组"), QStringLiteral("群名称"),
            QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty()) return;
        const QString description = QInputDialog::getMultiLineText(
            this, QStringLiteral("创建群组"), QStringLiteral("群描述"),
            QString(), &ok);
        if (ok) social_->createGroup(name, description);
    });
    connect(joinGroupButton, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const int id = QInputDialog::getInt(this, QStringLiteral("加入群组"),
                                            QStringLiteral("群 ID"), 1, 1,
                                            2147483647, 1, &ok);
        if (ok) social_->joinGroup(id);
    });
    connect(leaveGroupButton, &QPushButton::clicked, this, [this] {
        QListWidgetItem *item = groupList_->currentItem();
        if (!item) { showChatError(QStringLiteral("请先选择群组")); return; }
        if (QMessageBox::question(this, QStringLiteral("退出群组"),
                                  QStringLiteral("确认退出所选群组？")) == QMessageBox::Yes)
            social_->leaveGroup(item->data(Qt::UserRole).toInt());
    });
    connect(refreshGroupButton, &QPushButton::clicked,
            social_, &SocialController::refreshGroups);
    connect(network_, &NetworkClient::stateChanged,
            this, &MainWindow::updateNetworkState);
    showMessagesPage();
}

void MainWindow::setSession(const UserSession &session,
                            const ServerEndpoint &endpoint)
{
    session_ = session;
    endpoint_ = endpoint;
    avatarLabel_->setText(session_.userName.isEmpty()
                              ? QStringLiteral("?")
                              : session_.userName.left(1).toUpper());
    setWindowTitle(QStringLiteral("NexusIM · %1 (#%2) · %3")
                       .arg(session_.userName)
                       .arg(session_.userId)
                       .arg(session_.nodeId));
}

void MainWindow::reset()
{
    session_ = UserSession();
    contactList_->clear();
    groupList_->clear();
    chatWidget_->clearConversation();
    groupChatWidget_->clearGroup();
    aiChatWidget_->reset();
    avatarLabel_->setText(QStringLiteral("N"));
    setWindowTitle(QStringLiteral("NexusIM"));
}

void MainWindow::showGroups(const QJsonArray &groups)
{
    groupList_->clear();
    for (const QJsonValue &value : groups)
    {
        const QJsonObject group = value.toObject();
        const int id = group.value(QStringLiteral("id")).toInt(-1);
        if (id <= 0) continue;
        const QString name = group.value(QStringLiteral("name")).toString(
            QStringLiteral("群组 #%1").arg(id));
        const QString role = group.value(QStringLiteral("role")).toString();
        const int unread = social_->unreadCount(id);
        auto *item = new QListWidgetItem(
            unread > 0
                ? QStringLiteral("%1  [%2 条新消息]\n群 ID %3 · %4")
                      .arg(name).arg(unread).arg(id).arg(role)
                : QStringLiteral("%1\n群 ID %2 · %3").arg(name).arg(id).arg(role),
            groupList_);
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, name);
        item->setData(Qt::UserRole + 2,
                      group.value(QStringLiteral("description")).toString());
        item->setData(Qt::UserRole + 3, role);
        item->setData(Qt::UserRole + 4, unread);
        item->setSizeHint(QSize(220, 58));
    }
}

void MainWindow::showGroupUnread(int groupId, int count)
{
    for (int row = 0; row < groupList_->count(); ++row)
    {
        QListWidgetItem *item = groupList_->item(row);
        if (item->data(Qt::UserRole).toInt() != groupId) continue;
        item->setData(Qt::UserRole + 4, count);
        const QString name = item->data(Qt::UserRole + 1).toString();
        const QString role = item->data(Qt::UserRole + 3).toString();
        item->setText(count > 0
                          ? QStringLiteral("%1  [%2 条新消息]\n群 ID %3 · %4")
                                .arg(name).arg(count).arg(groupId).arg(role)
                          : QStringLiteral("%1\n群 ID %2 · %3")
                                .arg(name).arg(groupId).arg(role));
        return;
    }
}

void MainWindow::showContacts(const QJsonArray &contacts)
{
    contactList_->clear();
    for (const QJsonValue &value : contacts)
    {
        const QJsonObject contact = value.toObject();
        const int id = contact.value(QStringLiteral("id")).toInt(-1);
        if (id <= 0) continue;
        const QString name = contact.value(QStringLiteral("name")).toString(
            QStringLiteral("用户 #%1").arg(id));
        auto *item = new QListWidgetItem(
            QStringLiteral("%1\n用户 ID %2 · 状态未知").arg(name).arg(id), contactList_);
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, name);
        item->setSizeHint(QSize(220, 58));
    }
}

void MainWindow::showChatError(const QString &message)
{
    statusBar()->showMessage(message, 8000);
}

void MainWindow::updateNetworkState(NetworkClient::State state)
{
    switch (state)
    {
    case NetworkClient::State::Connected:
        connectionLabel_->setText(QStringLiteral("已连接")); break;
    case NetworkClient::State::Connecting:
        connectionLabel_->setText(QStringLiteral("连接中")); break;
    case NetworkClient::State::Disconnected:
        connectionLabel_->setText(QStringLiteral("连接已断开")); break;
    }
}

void MainWindow::showMessagesPage()
{
    middleStack_->setCurrentIndex(0);
    rightStack_->setCurrentIndex(0);
    activateNav(messagesButton_);
}

void MainWindow::showContactsPage()
{
    middleStack_->setCurrentIndex(1);
    rightStack_->setCurrentIndex(1);
    activateNav(contactsButton_);
}

void MainWindow::showGroupsPage()
{
    middleStack_->setCurrentIndex(2);
    rightStack_->setCurrentIndex(2);
    activateNav(groupsButton_);
}

void MainWindow::showAiPage()
{
    middleStack_->setCurrentIndex(3);
    rightStack_->setCurrentIndex(3);
    activateNav(aiButton_);
}

void MainWindow::startConversation(int userId)
{
    if (userId == session_.userId)
    {
        showChatError(QStringLiteral("不能给自己发消息"));
        return;
    }
    chat_->openConversation(userId);
    selectConversation(userId, QStringLiteral("用户 #%1").arg(userId));
}

void MainWindow::selectConversation(int userId, const QString &displayName)
{
    chat_->openConversation(userId, displayName);
    conversationList_->selectConversation(userId);
    chatWidget_->setConversation(userId, displayName);
    showMessagesPage();
}

void MainWindow::openContactConversation()
{
    QListWidgetItem *item = contactList_->currentItem();
    if (!item) return;
    selectConversation(item->data(Qt::UserRole).toInt(),
                       item->data(Qt::UserRole + 1).toString());
}

void MainWindow::openGroupConversation()
{
    QListWidgetItem *item = groupList_->currentItem();
    if (!item) return;
    groupChatWidget_->setGroup(item->data(Qt::UserRole).toInt(),
                               item->data(Qt::UserRole + 1).toString(),
                               item->data(Qt::UserRole + 2).toString(),
                               item->data(Qt::UserRole + 3).toString());
    middleStack_->setCurrentIndex(2);
    rightStack_->setCurrentIndex(2);
    activateNav(groupsButton_);
}

void MainWindow::openSettings()
{
    SettingsDialog dialog(endpoint_, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const ServerEndpoint changed = dialog.endpoint();
    if (changed.host == endpoint_.host && changed.port == endpoint_.port &&
        changed.connectTimeoutMs == endpoint_.connectTimeoutMs &&
        changed.requestTimeoutMs == endpoint_.requestTimeoutMs)
        return;
    if (QMessageBox::question(
            this, QStringLiteral("切换服务器"),
            QStringLiteral("修改连接入口需要注销当前会话，是否继续？")) ==
        QMessageBox::Yes)
        emit endpointChangeRequested(changed);
}

QWidget *MainWindow::makeUnavailablePage(const QString &title,
                                         const QString &detail)
{
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("emptyPage"));
    auto *mark = new QLabel(QStringLiteral("N"), page);
    mark->setObjectName(QStringLiteral("emptyMark"));
    mark->setAlignment(Qt::AlignCenter);
    auto *heading = new QLabel(title, page);
    heading->setObjectName(QStringLiteral("emptyTitle"));
    auto *text = new QLabel(detail, page);
    text->setObjectName(QStringLiteral("mutedLabel"));
    text->setWordWrap(true);
    text->setAlignment(Qt::AlignCenter);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 30, 30, 30);
    layout->addStretch();
    layout->addWidget(mark, 0, Qt::AlignCenter);
    layout->addWidget(heading, 0, Qt::AlignCenter);
    layout->addWidget(text, 0, Qt::AlignCenter);
    layout->addStretch();
    return page;
}

QPushButton *MainWindow::makeNavButton(const QString &text,
                                       const QString &tooltip)
{
    auto *button = new QPushButton(text, this);
    button->setObjectName(QStringLiteral("navButton"));
    button->setCheckable(true);
    button->setToolTip(tooltip);
    button->setFixedSize(44, 44);
    return button;
}

void MainWindow::activateNav(QPushButton *button)
{
    for (QPushButton *candidate : {messagesButton_, contactsButton_, groupsButton_, aiButton_})
        candidate->setChecked(candidate == button);
}

} // namespace client
} // namespace nexusim
