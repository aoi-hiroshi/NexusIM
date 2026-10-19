#include "network/networkclient.hpp"

#include <QJsonDocument>
#include <QJsonParseError>

namespace nexusim {
namespace client {

NetworkClient::NetworkClient(QObject *parent)
    : QObject(parent), codec_(FrameCodec::DefaultMaxFrameLength)
{
    connectTimer_.setSingleShot(true);
    connect(&connectTimer_, &QTimer::timeout,
            this, &NetworkClient::onConnectTimeout);
    connect(&socket_, &QTcpSocket::readyRead,
            this, &NetworkClient::onReadyRead);
    connect(&socket_, &QTcpSocket::connected,
            this, &NetworkClient::onConnected);
    connect(&socket_, &QTcpSocket::disconnected,
            this, &NetworkClient::onDisconnected);
    connect(&socket_, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
            this, &NetworkClient::onSocketError);
}

NetworkClient::State NetworkClient::state() const { return state_; }
bool NetworkClient::isConnected() const { return state_ == State::Connected; }
ServerEndpoint NetworkClient::endpoint() const { return endpoint_; }

void NetworkClient::connectToServer(const ServerEndpoint &endpoint)
{
    if (endpoint.host.trimmed().isEmpty() || endpoint.port == 0)
    {
        emit networkError(QStringLiteral("服务器地址或端口无效"));
        return;
    }
    intentionalDisconnect_ = true;
    connectTimer_.stop();
    socket_.abort();
    receiveBuffer_.clear();
    intentionalDisconnect_ = false;
    endpoint_ = endpoint;
    setState(State::Connecting);
    socket_.connectToHost(endpoint_.host, endpoint_.port);
    connectTimer_.start(qMax(1, endpoint_.connectTimeoutMs));
}

void NetworkClient::disconnectFromServer()
{
    intentionalDisconnect_ = true;
    connectTimer_.stop();
    if (socket_.state() == QAbstractSocket::UnconnectedState)
    {
        receiveBuffer_.clear();
        setState(State::Disconnected);
        intentionalDisconnect_ = false;
        return;
    }
    socket_.disconnectFromHost();
}

bool NetworkClient::sendJson(const QJsonObject &object, QString *error)
{
    if (!isConnected())
    {
        if (error) *error = QStringLiteral("尚未连接服务器");
        return false;
    }
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QString codecError;
    const QByteArray frame = codec_.encode(payload, &codecError);
    if (frame.isEmpty())
    {
        if (error) *error = codecError;
        return false;
    }
    const qint64 queued = socket_.write(frame);
    if (queued != frame.size())
    {
        if (error) *error = socket_.errorString().isEmpty()
                                ? QStringLiteral("消息未能完整写入网络缓冲区")
                                : socket_.errorString();
        return false;
    }
    if (error) error->clear();
    return true;
}

void NetworkClient::onReadyRead()
{
    receiveBuffer_.append(socket_.readAll());
    QList<QByteArray> frames;
    const FrameCodec::DecodeResult result = codec_.decode(receiveBuffer_, frames);
    if (result.status == FrameCodec::Status::InvalidLength)
    {
        emit protocolError(QStringLiteral("服务器返回非法帧长度：%1").arg(result.invalidLength));
        socket_.abort();
        return;
    }
    for (const QByteArray &frame : frames)
    {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(frame, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
        {
            emit protocolError(QStringLiteral("服务器返回了无效 JSON 消息"));
            socket_.abort();
            return;
        }
        emit messageReceived(document.object());
    }
}

void NetworkClient::onConnected()
{
    connectTimer_.stop();
    intentionalDisconnect_ = false;
    setState(State::Connected);
    emit connected();
}

void NetworkClient::onDisconnected()
{
    connectTimer_.stop();
    receiveBuffer_.clear();
    setState(State::Disconnected);
    emit disconnected();
    intentionalDisconnect_ = false;
}

void NetworkClient::onSocketError(QAbstractSocket::SocketError)
{
    if (!intentionalDisconnect_)
        emit networkError(socket_.errorString());
}

void NetworkClient::onConnectTimeout()
{
    if (state_ != State::Connecting) return;
    intentionalDisconnect_ = true;
    socket_.abort();
    setState(State::Disconnected);
    intentionalDisconnect_ = false;
    emit networkError(QStringLiteral("连接服务器超时"));
}

void NetworkClient::setState(State state)
{
    if (state_ == state) return;
    state_ = state;
    emit stateChanged(state_);
}

} // namespace client
} // namespace nexusim
