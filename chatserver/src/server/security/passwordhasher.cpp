#include "server/security/passwordhasher.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
constexpr int kIterations = 120000;
constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32;
constexpr const char *kPrefix = "pbkdf2_sha256";

std::string toHex(const unsigned char *data, std::size_t size)
{
    static const char digits[] = "0123456789abcdef";
    std::string result(size * 2, '0');
    for (std::size_t index = 0; index < size; ++index)
    {
        result[index * 2] = digits[(data[index] >> 4U) & 0x0fU];
        result[index * 2 + 1] = digits[data[index] & 0x0fU];
    }
    return result;
}

bool fromHex(const std::string &text, std::vector<unsigned char> &result)
{
    if (text.empty() || text.size() % 2 != 0)
    {
        return false;
    }
    auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };

    result.resize(text.size() / 2);
    for (std::size_t index = 0; index < result.size(); ++index)
    {
        const int high = nibble(text[index * 2]);
        const int low = nibble(text[index * 2 + 1]);
        if (high < 0 || low < 0)
        {
            return false;
        }
        result[index] = static_cast<unsigned char>((high << 4) | low);
    }
    return true;
}
} // namespace

namespace nexusim
{

std::string PasswordHasher::hash(const std::string &password)
{
    std::array<unsigned char, kSaltBytes> salt{};
    std::array<unsigned char, kHashBytes> derived{};
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1 ||
        PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                          salt.data(), static_cast<int>(salt.size()), kIterations,
                          EVP_sha256(), static_cast<int>(derived.size()), derived.data()) != 1)
    {
        throw std::runtime_error("password hashing failed");
    }

    return std::string(kPrefix) + "$" + std::to_string(kIterations) + "$" +
           toHex(salt.data(), salt.size()) + "$" + toHex(derived.data(), derived.size());
}

bool PasswordHasher::verify(const std::string &password, const std::string &encoded)
{
    std::istringstream input(encoded);
    std::string algorithm;
    std::string iterationText;
    std::string saltText;
    std::string hashText;
    if (!std::getline(input, algorithm, '$') || !std::getline(input, iterationText, '$') ||
        !std::getline(input, saltText, '$') || !std::getline(input, hashText) ||
        algorithm != kPrefix)
    {
        return false;
    }

    int iterations = 0;
    try
    {
        iterations = std::stoi(iterationText);
    }
    catch (...)
    {
        return false;
    }
    if (iterations < 10000 || iterations > 1000000)
    {
        return false;
    }

    std::vector<unsigned char> salt;
    std::vector<unsigned char> expected;
    if (!fromHex(saltText, salt) || !fromHex(hashText, expected) ||
        salt.size() != kSaltBytes || expected.size() != kHashBytes)
    {
        return false;
    }

    std::vector<unsigned char> actual(expected.size());
    if (PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                          salt.data(), static_cast<int>(salt.size()), iterations,
                          EVP_sha256(), static_cast<int>(actual.size()), actual.data()) != 1)
    {
        return false;
    }
    return CRYPTO_memcmp(actual.data(), expected.data(), expected.size()) == 0;
}

bool PasswordHasher::isEncoded(const std::string &value)
{
    return value.compare(0, std::char_traits<char>::length(kPrefix), kPrefix) == 0;
}

std::string generateSessionId()
{
    std::array<unsigned char, 32> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
    {
        throw std::runtime_error("secure session id generation failed");
    }
    return toHex(bytes.data(), bytes.size());
}

} // namespace nexusim
