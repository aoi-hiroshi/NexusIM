#include "controllers/sessioncontroller.hpp"

namespace nexusim {
namespace client {

SessionController::SessionController(QObject *parent) : QObject(parent) {}

UserSession SessionController::session() const { return session_; }
bool SessionController::isAuthenticated() const { return session_.authenticated; }

void SessionController::establish(const QJsonObject &loginResponse)
{
    session_.userId = loginResponse.value(QStringLiteral("id")).toInt(-1);
    session_.userName = loginResponse.value(QStringLiteral("name")).toString();
    session_.nodeId = loginResponse.value(QStringLiteral("node_id")).toString();
    session_.sessionId = loginResponse.value(QStringLiteral("session_id")).toString();
    session_.authenticated = session_.userId > 0 && !session_.userName.isEmpty() &&
                             session_.sessionId.size() == 64;
    if (session_.authenticated) emit sessionEstablished(loginResponse);
}

void SessionController::clear()
{
    const bool wasActive = session_.authenticated;
    session_ = UserSession();
    if (wasActive) emit sessionCleared();
}

} // namespace client
} // namespace nexusim
