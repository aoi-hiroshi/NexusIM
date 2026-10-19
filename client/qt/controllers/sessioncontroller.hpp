#ifndef NEXUSIM_QT_SESSIONCONTROLLER_HPP
#define NEXUSIM_QT_SESSIONCONTROLLER_HPP

#include <QJsonObject>
#include <QObject>

namespace nexusim {
namespace client {

struct UserSession
{
    int userId{-1};
    QString userName;
    QString nodeId;
    QString sessionId;
    bool authenticated{false};
};

class SessionController : public QObject
{
    Q_OBJECT
public:
    explicit SessionController(QObject *parent = nullptr);

    UserSession session() const;
    bool isAuthenticated() const;
    void establish(const QJsonObject &loginResponse);
    void clear();

signals:
    void sessionEstablished(const QJsonObject &loginResponse);
    void sessionCleared();

private:
    UserSession session_;
};

} // namespace client
} // namespace nexusim

#endif
