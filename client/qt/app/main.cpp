#include "app/applicationcontroller.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QFile>
#include <QSettings>

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NexusIM"));
    QApplication::setApplicationName(QStringLiteral("NexusIMClient"));
    QApplication::setApplicationVersion(QStringLiteral("0.5.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("NexusIM Qt5 desktop client"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption hostOption(QStringList{QStringLiteral("H"), QStringLiteral("host")},
                                  QStringLiteral("ChatServer or Nginx host"),
                                  QStringLiteral("host"));
    QCommandLineOption portOption(QStringList{QStringLiteral("p"), QStringLiteral("port")},
                                  QStringLiteral("TCP port"), QStringLiteral("port"));
    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.process(app);

    QSettings settings;
    nexusim::client::ServerEndpoint endpoint;
    endpoint.host = settings.value(QStringLiteral("server/host"),
                                   QStringLiteral("127.0.0.1")).toString();
    endpoint.port = static_cast<quint16>(
        settings.value(QStringLiteral("server/port"), 8000).toUInt());
    endpoint.connectTimeoutMs = settings.value(
        QStringLiteral("server/connectTimeoutMs"), 5000).toInt();
    endpoint.requestTimeoutMs = settings.value(
        QStringLiteral("server/requestTimeoutMs"), 5000).toInt();
    if (parser.isSet(hostOption)) endpoint.host = parser.value(hostOption);
    if (parser.isSet(portOption))
    {
        bool ok = false;
        const uint port = parser.value(portOption).toUInt(&ok);
        if (ok && port > 0 && port <= 65535) endpoint.port = static_cast<quint16>(port);
    }

    QFile style(QStringLiteral(":/styles/theme.qss"));
    if (style.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(style.readAll()));

    nexusim::client::ApplicationController controller(endpoint);
    controller.start();
    return app.exec();
}
