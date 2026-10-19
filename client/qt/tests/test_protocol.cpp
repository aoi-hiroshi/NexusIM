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
};

QTEST_APPLESS_MAIN(ProtocolTests)
#include "test_protocol.moc"
