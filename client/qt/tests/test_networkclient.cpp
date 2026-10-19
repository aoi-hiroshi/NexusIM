#include "network/networkclient.hpp"
#include "protocol/framecodec.hpp"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QtTest>

using namespace nexusim::client;

class NetworkClientTests : public QObject
{
    Q_OBJECT
private slots:
    void receivesHalfAndStickyFrames()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        NetworkClient client;
        QSignalSpy connected(&client, &NetworkClient::connected);
        QSignalSpy received(&client, &NetworkClient::messageReceived);
        ServerEndpoint endpoint;
        endpoint.host = QStringLiteral("127.0.0.1");
        endpoint.port = server.serverPort();
        endpoint.connectTimeoutMs = 1000;
        client.connectToServer(endpoint);
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 2000);
        QVERIFY(server.hasPendingConnections());
        QTcpSocket *peer = server.nextPendingConnection();
        QVERIFY(peer != nullptr);

        FrameCodec codec;
        const QByteArray first = codec.encode(QJsonDocument(
            QJsonObject{{"msgid", 12}, {"request_id", "half"}, {"ok", true}})
                                                  .toJson(QJsonDocument::Compact));
        const QByteArray second = codec.encode(QJsonDocument(
            QJsonObject{{"msgid", 6}, {"id", 7}, {"toid", 8}, {"msg", "a"}})
                                                   .toJson(QJsonDocument::Compact));
        const QByteArray third = codec.encode(QJsonDocument(
            QJsonObject{{"msgid", 6}, {"id", 7}, {"toid", 8}, {"msg", "b"}})
                                                  .toJson(QJsonDocument::Compact));
        peer->write(first.left(2));
        peer->flush();
        QTest::qWait(20);
        QCOMPARE(received.count(), 0);
        peer->write(first.mid(2) + second + third);
        peer->flush();
        QTRY_COMPARE_WITH_TIMEOUT(received.count(), 3, 2000);
        QCOMPARE(qvariant_cast<QJsonObject>(received.at(0).at(0))
                     .value("request_id").toString(), QStringLiteral("half"));
        peer->deleteLater();
    }

    void rejectsIllegalServerFrame()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        NetworkClient client;
        QSignalSpy connected(&client, &NetworkClient::connected);
        QSignalSpy protocolError(&client, &NetworkClient::protocolError);
        ServerEndpoint endpoint;
        endpoint.host = QStringLiteral("127.0.0.1");
        endpoint.port = server.serverPort();
        endpoint.connectTimeoutMs = 1000;
        client.connectToServer(endpoint);
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 2000);
        QTcpSocket *peer = server.nextPendingConnection();
        QVERIFY(peer != nullptr);
        peer->write(QByteArray(4, '\0'));
        peer->flush();
        QTRY_COMPARE_WITH_TIMEOUT(protocolError.count(), 1, 2000);
        QTRY_COMPARE_WITH_TIMEOUT(client.state(), NetworkClient::State::Disconnected, 2000);
        peer->deleteLater();
    }

    void observesRemoteDisconnect()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        NetworkClient client;
        QSignalSpy connected(&client, &NetworkClient::connected);
        QSignalSpy disconnected(&client, &NetworkClient::disconnected);
        ServerEndpoint endpoint;
        endpoint.host = QStringLiteral("127.0.0.1");
        endpoint.port = server.serverPort();
        endpoint.connectTimeoutMs = 1000;
        client.connectToServer(endpoint);
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 2000);
        QTcpSocket *peer = server.nextPendingConnection();
        QVERIFY(peer != nullptr);
        peer->disconnectFromHost();
        QTRY_COMPARE_WITH_TIMEOUT(disconnected.count(), 1, 2000);
        QCOMPARE(client.state(), NetworkClient::State::Disconnected);
        peer->deleteLater();
    }

    void reportsConnectionRefusal()
    {
        QTcpServer reservation;
        QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
        const quint16 unusedPort = reservation.serverPort();
        reservation.close();
        NetworkClient client;
        QSignalSpy error(&client, &NetworkClient::networkError);
        ServerEndpoint endpoint;
        endpoint.host = QStringLiteral("127.0.0.1");
        endpoint.port = unusedPort;
        endpoint.connectTimeoutMs = 500;
        client.connectToServer(endpoint);
        QTRY_VERIFY_WITH_TIMEOUT(error.count() >= 1, 2000);
    }
};

QTEST_GUILESS_MAIN(NetworkClientTests)
#include "test_networkclient.moc"
