#include "controllers/aicontroller.hpp"
#include "controllers/chatcontroller.hpp"
#include "controllers/sessioncontroller.hpp"
#include "controllers/socialcontroller.hpp"
#include "network/networkclient.hpp"
#include "network/requestmanager.hpp"
#include "ui/groupchatwidget.hpp"
#include "ui/mainwindow.hpp"

#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QtTest>

using namespace nexusim::client;

class SocialUiTests : public QObject
{
    Q_OBJECT
private slots:
    void exposesRealFriendAndGroupControls()
    {
        NetworkClient network;
        RequestManager requests;
        SessionController session;
        ChatController chat(&network, &requests, &session);
        SocialController social(&network, &requests, &session);
        AIController ai(&network, &requests, &session, 90000);
        MainWindow window(&chat, &social, &ai, &network);

        QStringList buttonTexts;
        for (QPushButton *button : window.findChildren<QPushButton *>())
            buttonTexts.push_back(button->text());
        QVERIFY(buttonTexts.contains(QStringLiteral("添加好友")));
        QVERIFY(buttonTexts.contains(QStringLiteral("创建")));
        QVERIFY(buttonTexts.contains(QStringLiteral("加入")));
        QVERIFY(buttonTexts.contains(QStringLiteral("退出")));

        window.showContacts(QJsonArray{
            QJsonObject{{"id", 8}, {"name", QStringLiteral("好友甲")}}});
        window.showGroups(QJsonArray{
            QJsonObject{{"id", 9}, {"name", QStringLiteral("群甲")},
                        {"description", QStringLiteral("真实群")},
                        {"role", QStringLiteral("creator")}}});
        bool friendVisible = false;
        bool groupVisible = false;
        for (QListWidget *list : window.findChildren<QListWidget *>())
        {
            for (int row = 0; row < list->count(); ++row)
            {
                friendVisible = friendVisible || list->item(row)->text().contains(
                    QStringLiteral("好友甲"));
                groupVisible = groupVisible || list->item(row)->text().contains(
                    QStringLiteral("群甲"));
            }
        }
        QVERIFY(friendVisible);
        QVERIFY(groupVisible);
    }

    void groupChatWidgetShowsSelectedGroup()
    {
        NetworkClient network;
        RequestManager requests;
        SessionController session;
        SocialController social(&network, &requests, &session);
        GroupChatWidget widget(&social);
        widget.setGroup(42, QStringLiteral("跨节点群"),
                        QStringLiteral("测试描述"), QStringLiteral("normal"));
        bool titleFound = false;
        for (QLabel *label : widget.findChildren<QLabel *>())
            titleFound = titleFound || label->text().contains(QStringLiteral("跨节点群"));
        QVERIFY(titleFound);
    }
};

QTEST_MAIN(SocialUiTests)
#include "test_socialui.moc"
