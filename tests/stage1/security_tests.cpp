#include "server/security/passwordhasher.hpp"

#include <cstdlib>
#include <iostream>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << std::endl;
        std::exit(1);
    }
}
} // namespace

int main()
{
    const std::string first = nexusim::PasswordHasher::hash("correct horse battery staple");
    const std::string second = nexusim::PasswordHasher::hash("correct horse battery staple");
    require(first != second, "a fresh random salt must change the encoded hash");
    require(nexusim::PasswordHasher::isEncoded(first), "hash must be versioned");
    require(nexusim::PasswordHasher::verify("correct horse battery staple", first),
            "the correct password must verify");
    require(!nexusim::PasswordHasher::verify("wrong password", first),
            "an incorrect password must fail");
    require(!nexusim::PasswordHasher::verify("password", "malformed"),
            "malformed stored values must fail closed");

    const std::string sessionOne = nexusim::generateSessionId();
    const std::string sessionTwo = nexusim::generateSessionId();
    require(sessionOne.size() == 64 && sessionTwo.size() == 64,
            "session ids must encode 256 random bits");
    require(sessionOne != sessionTwo, "session ids must not be reused");

    std::cout << "stage1_security_tests: PASS" << std::endl;
    return 0;
}
