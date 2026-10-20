#include "ui/mainwindow.hpp"

#include "controllers/chatcontroller.hpp"
#include "controllers/aicontroller.hpp"
#include "ui/aichatwidget.hpp"
#include "ui/chatwidget.hpp"
#include "ui/conversationlist.hpp"
#include "ui/settingsdialog.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>

namespace nexusim {
namespace client {

MainWindow::MainWindow(ChatController *chat, AIController *ai,
                       NetworkClient *network,
                       QWidget *parent)
    : QMainWindow(parent), chat_(chat), ai_(ai), network_(network),
      avatarLabel_(new QLabel(QStringLiteral("N"), this)),
      connectionLabel_(new QLabel(QStringLiteral("未连接"), this)),
      middleStack_(new QStackedWidget(this)), rightStack_(new QStackedWidget(this)),
      conversationList_(new ConversationList(chat_->conversations(), this)),
      chatWidget_(new ChatWidget(chat_, chat_->conversations(), this)),
      aiChatWidget_(new AIChatWidget(ai_, this)),
      contactList_(new QListWidget(this))
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
        QStringLiteral("好友列表来自登录响应；在线状态未由当前协议保证。双击联系人开始聊天。"),
        contactPanel);
    contactHint->setWordWrap(true);
    contactHint->setObjectName(QStringLiteral("mutedLabel"));
    contactList_->setFrameShape(QFrame::NoFrame);
    auto *contactLayout = new QVBoxLayout(contactPanel);
    contactLayout->setContentsMargins(14, 18, 14, 12);
    contactLayout->addWidget(contactTitle);
    contactLayout->addWidget(contactHint);
    contactLayout->addWidget(contactList_, 1);
    middleStack_->addWidget(contactPanel);
    middleStack_->addWidget(makeUnavailablePage(
        QStringLiteral("群组"),
        QStringLiteral("服务端群组 Handler 尚未通过安全审计，Stage 4 不会擅自启用。")));
    middleStack_->addWidget(makeUnavailablePage(
        QStringLiteral("AI 助手"),
        QStringLiteral("DeepSeek V4 Pro 由独立 AIService 调用；对话上下文在服务重启后丢失。")));

    rightStack_->addWidget(chatWidget_);
    rightStack_->addWidget(makeUnavailablePage(
        QStringLiteral("联系人"), QStringLiteral("双击左侧联系人或输入用户 ID 发起会话。")));
    rightStack_->addWidget(makeUnavailablePage(
        QStringLiteral("群组暂未开放"),
        QStringLiteral("当前客户端不会发送 CREATE_GROUP、ADD_GROUP 或 GROUP_CHAT 请求。")));
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
    chatWidget_->clearConversation();
    aiChatWidget_->reset();
    avatarLabel_->setText(QStringLiteral("N"));
    setWindowTitle(QStringLiteral("NexusIM"));
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
