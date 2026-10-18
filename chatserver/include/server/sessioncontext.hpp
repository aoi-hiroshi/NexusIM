#ifndef NEXUSIM_SESSIONCONTEXT_H
#define NEXUSIM_SESSIONCONTEXT_H

#include <string>

namespace nexusim
{

struct SessionContext
{
    std::string connectionId;
    // Random per-connection generation token, present before authentication.
    // It prevents a late unauthenticated RPC from completing on a reused name.
    std::string lifecycleId;
    std::string sessionId;
    int userId{-1};
    std::string userName;
    bool authenticated{false};
};

} // namespace nexusim

#endif
