#ifndef NEXUSIM_PASSWORDHASHER_H
#define NEXUSIM_PASSWORDHASHER_H

#include <string>

namespace nexusim
{

class PasswordHasher
{
public:
    static std::string hash(const std::string &password);
    static bool verify(const std::string &password, const std::string &encoded);
    static bool isEncoded(const std::string &value);
};

std::string generateSessionId();

} // namespace nexusim

#endif
