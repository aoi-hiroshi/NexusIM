#include "protocol/messageprotocol.hpp"

#include <QJsonDocument>
#include <QtTest>

using namespace nexusim::client;

class ProtocolTests : public QObject
{
    Q_OBJECT
private slots:
    void requestShapesMatchServer()
    {
        const QJsonObject login = protocol::loginRequest(42, QStringLiteral("secret"));
        QCOMPARE(login.value("msgid").toInt(), 1);
        QCOMPARE(login.value("id").toInt(), 42);
        QCOMPARE(login.value("password").toString(), QStringLiteral("secret"));

        const QJsonObject registration = protocol::registerRequest(
            QStringLiteral("测试用户"), QStringLiteral("password"));
        QCOMPARE(registration.value("msgid").toInt(), 4);
        QVERIFY(registration.contains("name"));

        const QJsonObject chat = protocol::directChatRequest(
            9, QStringLiteral("你好"), QStringLiteral("2026-10-19 12:00:00"));
        QCOMPARE(chat.value("msgid").toInt(), 6);
        QCOMPARE(chat.value("toid").toInt(), 9);
        QVERIFY(!chat.contains("id"));
        QVERIFY(!chat.contains("session_id"));

        const QJsonObject ai = protocol::aiChatRequest(
            QStringLiteral("req-1"), QStringLiteral("conversation-1"),
            QStringLiteral("请用中文回答"));
        QCOMPARE(ai.value("msgid").toInt(), 14);
        QCOMPARE(ai.value("request_id").toString(), QStringLiteral("req-1"));
        QCOMPARE(ai.value("conversation_id").toString(),
                 QStringLiteral("conversation-1"));
        QVERIFY(!ai.contains("user_id"));
        QVERIFY(!ai.contains("session_id"));

        const QJsonObject addFriend = protocol::addFriendRequest(
            QStringLiteral("friend-1"), 88);
        QCOMPARE(addFriend.value("msgid").toInt(), 7);
        QCOMPARE(addFriend.value("friendid").toInt(), 88);
        QVERIFY(!addFriend.contains("id"));

        const QJsonObject group = protocol::groupChatRequest(
            QStringLiteral("group-1"), 9, QStringLiteral("群消息"),
            QStringLiteral("2026-10-20 12:00:00"));
        QCOMPARE(group.value("msgid").toInt(), 10);
        QCOMPARE(group.value("groupid").toInt(), 9);
        QVERIFY(!group.contains("id"));
        QVERIFY(!group.contains("name"));
    }

    void decodesLegacyStringArrays()
    {
        QJsonArray array;
        array.push_back(QString::fromUtf8(QJsonDocument(
            QJsonObject{{"id", 7}, {"name", QStringLiteral("七号")}})
                                              .toJson(QJsonDocument::Compact)));
        array.push_back(QStringLiteral("not-json"));
        array.push_back(QJsonObject{{"id", 8}, {"name", QStringLiteral("八号")}});
        const QVector<QJsonObject> decoded = protocol::decodeObjectStrings(array);
        QCOMPARE(decoded.size(), 2);
        QCOMPARE(decoded.at(0).value("id").toInt(), 7);
        QCOMPARE(decoded.at(1).value("id").toInt(), 8);
    }

    void formatsStructuredErrors()
    {
        const QString text = protocol::responseErrorText(
            QJsonObject{{"error", "FEATURE_DISABLED"},
                        {"errmsg", "message type is not enabled"}});
        QVERIFY(text.contains(QStringLiteral("FEATURE_DISABLED")));
        QVERIFY(text.contains(QStringLiteral("message type")));
    }

    void aiMessageIdsRemainCompatible()
    {
        QCOMPARE(static_cast<int>(protocol::AIChat), 14);
        QCOMPARE(static_cast<int>(protocol::AIChatAck), 15);
        QCOMPARE(static_cast<int>(protocol::AddFriendAck), 16);
        QCOMPARE(static_cast<int>(protocol::GroupChatAck), 27);
    }
};

QTEST_APPLESS_MAIN(ProtocolTests)
#include "test_protocol.moc"
