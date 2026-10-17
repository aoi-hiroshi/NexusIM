#ifndef NEXUSIM_SESSIONCONTEXT_H
#define NEXUSIM_SESSIONCONTEXT_H

#include <string>

namespace nexusim
{

struct SessionContext
{
    std::string connectionId;
    std::string sessionId;
    int userId{-1};
    std::string userName;
    bool authenticated{false};
};

} // namespace nexusim

#endif
