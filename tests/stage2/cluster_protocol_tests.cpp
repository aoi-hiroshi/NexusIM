#include "server/redis/clusterprotocol.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main()
{
    const std::string session(64, 'a');
    require(nexusim::isValidNodeId("chatserver-1"), "valid node id rejected");
    require(!nexusim::isValidNodeId("chatserver:1"), "invalid node id accepted");
    require(nexusim::nodeChannel("chatserver-1") ==
                "nexusim:chat:node:chatserver-1",
            "node channel mismatch");
    require(nexusim::isValidSessionId(session), "valid session rejected");
    require(!nexusim::isValidSessionId(std::string(64, 'z')),
            "non-hex session accepted");
    require(!nexusim::isValidConnectionId("connection|ambiguous"),
            "ambiguous connection id accepted");

    const nexusim::PresenceRecord originalPresence{
        "chatserver-2", "NexusIM-127.0.0.1:6002#7", session};
    const auto presence = nexusim::decodePresence(
        nexusim::encodePresence(originalPresence));
    require(presence.has_value(), "Presence round trip failed");
    require(presence->nodeId == originalPresence.nodeId,
            "Presence node changed");
    require(presence->connectionId == originalPresence.connectionId,
            "Presence connection changed");
    require(presence->sessionId == session, "Presence session changed");
    require(!nexusim::decodePresence("chatserver-1|connection|short"),
            "short Presence session accepted");
    require(!nexusim::decodePresence("bad:node|connection|" + session),
            "invalid Presence node accepted");

    const std::string chatPayload =
        R"({"msgid":6,"toid":42,"msg":"line 1\n\"quoted\""})";
    const nexusim::ClusterDelivery originalDelivery{42, session, chatPayload};
    const auto delivery = nexusim::decodeDelivery(
        nexusim::encodeDelivery(originalDelivery));
    require(delivery.has_value(), "delivery round trip failed");
    require(delivery->recipientId == 42, "delivery recipient changed");
    require(delivery->expectedSessionId == session,
            "delivery session changed");
    require(delivery->payload == chatPayload, "delivery payload changed");
    require(!nexusim::decodeDelivery("not-json"), "invalid JSON accepted");
    require(!nexusim::decodeDelivery(
                R"({"version":2,"recipient_id":42,"expected_session_id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","payload":"x"})"),
            "unsupported delivery version accepted");
    require(!nexusim::decodeDelivery(
                R"({"version":1,"recipient_id":0,"expected_session_id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","payload":"x"})"),
            "invalid delivery recipient accepted");

    std::cout << "stage2_cluster_protocol_tests: PASS\n";
    return 0;
}
