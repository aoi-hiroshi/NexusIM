#ifndef NEXUSIM_QT_NETWORKCLIENT_HPP
#define NEXUSIM_QT_NETWORKCLIENT_HPP

#include "protocol/framecodec.hpp"

#include <QJsonObject>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>

namespace nexusim {
namespace client {

struct ServerEndpoint
{
    QString host{QStringLiteral("127.0.0.1")};
    quint16 port{8000};
    int connectTimeoutMs{5000};
    int requestTimeoutMs{5000};
};

class NetworkClient : public QObject
{
    Q_OBJECT
public:
    enum class State { Disconnected, Connecting, Connected };
    Q_ENUM(State)

    explicit NetworkClient(QObject *parent = nullptr);

    State state() const;
    bool isConnected() const;
    ServerEndpoint endpoint() const;
    void connectToServer(const ServerEndpoint &endpoint);
    void disconnectFromServer();
    bool sendJson(const QJsonObject &object, QString *error = nullptr);

signals:
    void stateChanged(nexusim::client::NetworkClient::State state);
    void connected();
    void disconnected();
    void messageReceived(const QJsonObject &message);
    void networkError(const QString &message);
    void protocolError(const QString &message);

private slots:
    void onReadyRead();
    void onConnected();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onConnectTimeout();

private:
    void setState(State state);

    QTcpSocket socket_;
    QTimer connectTimer_;
    QByteArray receiveBuffer_;
    FrameCodec codec_;
    ServerEndpoint endpoint_;
    State state_{State::Disconnected};
    bool intentionalDisconnect_{false};
};

} // namespace client
} // namespace nexusim

Q_DECLARE_METATYPE(nexusim::client::NetworkClient::State)
Q_DECLARE_METATYPE(nexusim::client::ServerEndpoint)

#endif
