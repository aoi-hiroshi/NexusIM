#include "ai_types.hpp"
#include "deepseek_client.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    const std::string expected = argc > 1 ? argv[1] : "AI_OK";
    nexusim::ai_service::DeepSeekClient client;
    const std::vector<nexusim::ai_service::AIMessage> messages{
        {"system", "This is an automated NexusIM mock probe."},
        {"user", "Return the configured mock result."}};
    const auto result = client.chat(messages, 2000);
    const std::string actual = nexusim::ai_service::statusCodeName(result.status);
    if (actual != expected)
    {
        std::cerr << "stage5_deepseek_client_probe: expected=" << expected
                  << " actual=" << actual
                  << " http_status=" << result.httpStatus
                  << " error=" << result.errorMessage << std::endl;
        return 1;
    }
    std::cout << "stage5_deepseek_client_probe: PASS status=" << actual
              << " http_status=" << result.httpStatus
              << " content_bytes=" << result.content.size() << std::endl;
    return 0;
}
