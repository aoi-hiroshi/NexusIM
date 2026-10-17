#ifndef NEXUSIM_CLUSTERPROTOCOL_H
#define NEXUSIM_CLUSTERPROTOCOL_H

#include "json.hpp"

#include <cctype>
#include <optional>
#include <string>

namespace nexusim
{

struct PresenceRecord
{
    std::string nodeId;
    std::string connectionId;
    std::string sessionId;
};

struct ClusterDelivery
{
    int recipientId{-1};
    std::string expectedSessionId;
    std::string payload;
};

inline bool isValidNodeId(const std::string &nodeId)
{
    if (nodeId.empty() || nodeId.size() > 64) return false;
    for (const unsigned char value : nodeId)
    {
        if (!std::isalnum(value) && value != '-' && value != '_' && value != '.')
        {
            return false;
        }
    }
    return true;
}

inline bool isValidSessionId(const std::string &sessionId)
{
    if (sessionId.size() != 64) return false;
    for (const unsigned char value : sessionId)
    {
        if (!std::isxdigit(value)) return false;
    }
    return true;
}

inline bool isValidConnectionId(const std::string &connectionId)
{
    return !connectionId.empty() && connectionId.size() <= 256 &&
           connectionId.find('|') == std::string::npos;
}

inline std::string nodeChannel(const std::string &nodeId)
{
    return "nexusim:chat:node:" + nodeId;
}

inline std::string encodePresence(const PresenceRecord &record)
{
    return record.nodeId + "|" + record.connectionId + "|" + record.sessionId;
}

inline std::optional<PresenceRecord> decodePresence(const std::string &value)
{
    const auto first = value.find('|');
    const auto second = first == std::string::npos ? std::string::npos
                                                   : value.find('|', first + 1);
    if (first == std::string::npos || second == std::string::npos ||
        value.find('|', second + 1) != std::string::npos)
    {
        return std::nullopt;
    }
    PresenceRecord record{value.substr(0, first),
                          value.substr(first + 1, second - first - 1),
                          value.substr(second + 1)};
    if (!isValidNodeId(record.nodeId) ||
        !isValidConnectionId(record.connectionId) ||
        !isValidSessionId(record.sessionId))
    {
        return std::nullopt;
    }
    return record;
}

inline std::string encodeDelivery(const ClusterDelivery &delivery)
{
    nlohmann::json value;
    value["version"] = 1;
    value["recipient_id"] = delivery.recipientId;
    value["expected_session_id"] = delivery.expectedSessionId;
    value["payload"] = delivery.payload;
    return value.dump();
}

inline std::optional<ClusterDelivery> decodeDelivery(const std::string &encoded)
{
    try
    {
        const auto value = nlohmann::json::parse(encoded);
        const auto version = value.find("version");
        const auto recipient = value.find("recipient_id");
        const auto session = value.find("expected_session_id");
        const auto payload = value.find("payload");
        if (version == value.end() || !version->is_number_integer() ||
            version->get<int>() != 1 || recipient == value.end() ||
            !recipient->is_number_integer() || recipient->get<int>() <= 0 ||
            session == value.end() || !session->is_string() ||
            !isValidSessionId(session->get<std::string>()) || payload == value.end() ||
            !payload->is_string() || payload->get<std::string>().empty())
        {
            return std::nullopt;
        }
        return ClusterDelivery{recipient->get<int>(), session->get<std::string>(),
                               payload->get<std::string>()};
    }
    catch (...)
    {
        return std::nullopt;
    }
}

} // namespace nexusim

#endif
