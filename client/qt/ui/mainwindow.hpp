#ifndef NEXUSIM_QT_MAINWINDOW_HPP
#define NEXUSIM_QT_MAINWINDOW_HPP

#include "controllers/sessioncontroller.hpp"
#include "network/networkclient.hpp"

#include <QJsonArray>
#include <QMainWindow>

class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace nexusim {
namespace client {

class ChatController;
class ChatWidget;
class ConversationList;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(ChatController *chat, NetworkClient *network,
               QWidget *parent = nullptr);

    void setSession(const UserSession &session, const ServerEndpoint &endpoint);
    void reset();

signals:
    void logoutRequested();
    void reconnectRequested();
    void endpointChangeRequested(const nexusim::client::ServerEndpoint &endpoint);

public slots:
    void showContacts(const QJsonArray &contacts);
    void showChatError(const QString &message);
    void updateNetworkState(NetworkClient::State state);

private slots:
    void showMessagesPage();
    void showContactsPage();
    void showGroupsPage();
    void showAiPage();
    void startConversation(int userId);
    void selectConversation(int userId, const QString &displayName);
    void openContactConversation();
    void openSettings();

private:
    QWidget *makeUnavailablePage(const QString &title, const QString &detail);
    QPushButton *makeNavButton(const QString &text, const QString &tooltip);
    void activateNav(QPushButton *button);

    ChatController *chat_;
    NetworkClient *network_;
    ServerEndpoint endpoint_;
    UserSession session_;
    QLabel *avatarLabel_;
    QLabel *connectionLabel_;
    QStackedWidget *middleStack_;
    QStackedWidget *rightStack_;
    ConversationList *conversationList_;
    ChatWidget *chatWidget_;
    QListWidget *contactList_;
    QPushButton *messagesButton_;
    QPushButton *contactsButton_;
    QPushButton *groupsButton_;
    QPushButton *aiButton_;
};

} // namespace client
} // namespace nexusim

#endif
