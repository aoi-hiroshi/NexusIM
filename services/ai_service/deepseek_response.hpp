#pragma once

#include "ai_types.hpp"

#include <string>

namespace nexusim
{
namespace ai_service
{

class DeepSeekResponseParser
{
public:
    static AIResult parse(long httpStatus, const std::string &body,
                          const std::string &upstreamRequestId = "");
};

} // namespace ai_service
} // namespace nexusim
