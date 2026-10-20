#include "public.hpp"
#include "server/redis/clusterprotocol.hpp"

#include <iostream>
#include <string>

int main()
{
    static_assert(ADD_FRIEND_MSG == 7, "legacy friend request id changed");
    static_assert(CREATE_GROUP_MSG == 8, "legacy create-group id changed");
    static_assert(ADD_GROUP_MSG == 9, "legacy join-group id changed");
    static_assert(GROUP_CHAT_MSG == 10, "legacy group-chat id changed");
    static_assert(AI_CHAT_MSG == 14 && AI_CHAT_MSG_ACK == 15,
                  "Stage 5 AI ids changed");
    static_assert(ADD_FRIEND_MSG_ACK == 16 && GROUP_CHAT_MSG_ACK == 27,
                  "Stage 6 response id range changed");

    nlohmann::json payload{{"msgid", GROUP_CHAT_MSG}, {"groupid", 9},
                           {"id", 7}, {"name", "sender"},
                           {"toid", 8}, {"msg", "hello"}};
    nexusim::ClusterDelivery delivery{
        8, std::string(64, 'a'), payload.dump()};
    const auto decoded = nexusim::decodeDelivery(nexusim::encodeDelivery(delivery));
    if (!decoded || decoded->recipientId != 8 ||
        decoded->expectedSessionId != delivery.expectedSessionId ||
        decoded->payload != delivery.payload)
    {
        std::cerr << "Stage 6 cluster envelope round trip failed\n";
        return 1;
    }
    std::cout << "stage6_protocol_tests: PASS\n";
    return 0;
}
