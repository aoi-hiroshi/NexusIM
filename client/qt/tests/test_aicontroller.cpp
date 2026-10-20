#include "controllers/aicontroller.hpp"
#include "controllers/sessioncontroller.hpp"
#include "network/networkclient.hpp"
#include "network/requestmanager.hpp"
#include "protocol/framecodec.hpp"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

using namespace nexusim::client;

class AIControllerTests : public QObject
{
    Q_OBJECT
private:
    static QJsonObject readFrame(QTcpSocket *socket)
    {
        QByteArray bytes;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 1500)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            bytes.append(socket->readAll());
            if (bytes.size() < 4) continue;
            const auto *header = reinterpret_cast<const unsigned char *>(
                bytes.constData());
            const quint32 length = (quint32(header[0]) << 24U) |
                                   (quint32(header[1]) << 16U) |
                                   (quint32(header[2]) << 8U) |
                                   quint32(header[3]);
            if (bytes.size() >= 4 + static_cast<int>(length))
                return QJsonDocument::fromJson(bytes.mid(4, length)).object();
            QTest::qWait(5);
        }
        return QJsonObject();
    }

    static void writeFrame(QTcpSocket *socket, const QJsonObject &object)
    {
        FrameCodec codec;
        socket->write(codec.encode(
            QJsonDocument(object).toJson(QJsonDocument::Compact)));
        socket->flush();
    }

private slots:
    void routesSuccessfulReplyByRequestAndConversation()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        NetworkClient network;
        RequestManager requests;
        SessionController session;
        AIController ai(&network, &requests, &session, 1000);
        connect(&network, &NetworkClient::messageReceived,
                &requests, &RequestManager::handleMessage);
        session.establish(QJsonObject{{"id", 7}, {"name", "user-seven"},
                                      {"session_id", QString(64, 'a')}});
        QVERIFY(session.isAuthenticated());
        ai.startSession();

        ServerEndpoint endpoint;
        endpoint.host = QStringLiteral("127.0.0.1");
        endpoint.port = server.serverPort();
        network.connectToServer(endpoint);
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
        QTRY_VERIFY_WITH_TIMEOUT(network.isConnected(), 1000);

        QSignalSpy accepted(&ai, &AIController::promptAccepted);
        QSignalSpy replied(&ai, &AIController::replyReceived);
        ai.sendPrompt(QStringLiteral("你好，AI"));
        QTRY_COMPARE_WITH_TIMEOUT(accepted.count(), 1, 500);
        const QJsonObject request = readFrame(peer.data());
        QCOMPARE(request.value("msgid").toInt(), 14);
        QVERIFY(!request.contains("user_id"));
        const QString requestId = request.value("request_id").toString();
        const QString conversationId = request.value("conversation_id").toString();
        QVERIFY(!requestId.isEmpty());
        QCOMPARE(conversationId, ai.conversationId());

        writeFrame(peer.data(), QJsonObject{
            {"msgid", 15}, {"ok", true}, {"request_id", requestId},
            {"conversation_id", conversationId},
            {"content", QStringLiteral("你好，来自 DeepSeek")},
            {"model", "deepseek-v4-pro"}, {"finish_reason", "stop"}});
        QTRY_COMPARE_WITH_TIMEOUT(replied.count(), 1, 1000);
        QCOMPARE(replied.at(0).at(1).toString(),
                 QStringLiteral("你好，来自 DeepSeek"));
        QVERIFY(!ai.isBusy());
    }

    void newConversationCancelsPendingRequest()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        NetworkClient network;
        RequestManager requests;
        SessionController session;
        AIController ai(&network, &requests, &session, 1000);
        connect(&network, &NetworkClient::messageReceived,
                &requests, &RequestManager::handleMessage);
        session.establish(QJsonObject{{"id", 8}, {"name", "user-eight"},
                                      {"session_id", QString(64, 'b')}});
        ai.startSession();
        ServerEndpoint endpoint;
        endpoint.host = QStringLiteral("127.0.0.1");
        endpoint.port = server.serverPort();
        network.connectToServer(endpoint);
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
        QScopedPointer<QTcpSocket> peer(server.nextPendingConnection());
        QTRY_VERIFY_WITH_TIMEOUT(network.isConnected(), 1000);

        ai.sendPrompt(QStringLiteral("慢请求"));
        QTRY_VERIFY_WITH_TIMEOUT(ai.isBusy(), 500);
        const QString oldConversation = ai.conversationId();
        ai.newConversation();
        QVERIFY(!ai.isBusy());
        QVERIFY(ai.conversationId() != oldConversation);
        QCOMPARE(requests.pendingCount(), 0);
    }
};

QTEST_GUILESS_MAIN(AIControllerTests)
#include "test_aicontroller.moc"
