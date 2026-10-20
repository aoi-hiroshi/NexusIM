#include "controllers/sessioncontroller.hpp"
#include "controllers/socialcontroller.hpp"
#include "network/networkclient.hpp"
#include "network/requestmanager.hpp"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

using namespace nexusim::client;

class SocialControllerTests : public QObject
{
    Q_OBJECT
private slots:
    void restoresOfflineGroupMessagesAndNeverTrustsClientIdentity()
    {
        NetworkClient network;
        RequestManager requests;
        SessionController session;
        session.establish(QJsonObject{{"id", 7}, {"name", "alice"},
                                      {"node_id", "chatserver-1"},
                                      {"session_id", QString(64, QLatin1Char('a'))}});
        SocialController social(&network, &requests, &session);
        QSignalSpy changed(&social, &SocialController::groupMessagesChanged);

        const QJsonObject offline{{"msgid", 10}, {"groupid", 23},
                                  {"id", 8}, {"name", QStringLiteral("测试成员")},
                                  {"msg", QStringLiteral("离线群消息")},
                                  {"time", "2026-10-20 12:00:00"}};
        QJsonArray encoded;
        encoded.push_back(QString::fromUtf8(
            QJsonDocument(offline).toJson(QJsonDocument::Compact)));
        social.startSession(QJsonObject{{"offlinemsg", encoded}});

        QCOMPARE(social.messages(23).size(), 1);
        QVERIFY(social.messages(23).first().offline);
        QCOMPARE(social.messages(23).first().senderId, 8);
        QVERIFY(changed.count() >= 1);
    }
};

QTEST_APPLESS_MAIN(SocialControllerTests)
#include "test_socialcontroller.moc"
