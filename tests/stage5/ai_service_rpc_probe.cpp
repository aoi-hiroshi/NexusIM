#include "ai_service.pb.h"
#include "mprpcapplication.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"

#include <chrono>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    MprpcApplication::Init(argc, argv);
    MprpcChannel channel;
    nexusim::ai::AIService_Stub stub(&channel);
    nexusim::ai::AIChatRequest request;
    request.set_request_id("stage5-rpc-probe");
    request.set_user_id(1);
    request.set_conversation_id("stage5-rpc-probe");
    request.set_prompt("Reply with a short test acknowledgement.");
    request.set_deadline_ms(85000);
    nexusim::ai::AIChatResponse response;
    MprpcController controller;
    controller.SetDeadline(MprpcController::Clock::now() +
                           std::chrono::milliseconds(85000));
    stub.Chat(&controller, &request, &response, nullptr);
    if (controller.Failed())
    {
        std::cerr << "stage5_ai_service_rpc_probe: RPC FAILED: "
                  << controller.ErrorText() << std::endl;
        return 2;
    }
    if (response.request_id() != request.request_id() ||
        response.conversation_id() != request.conversation_id())
    {
        std::cerr << "stage5_ai_service_rpc_probe: identity mismatch" << std::endl;
        return 3;
    }
    std::cout << "stage5_ai_service_rpc_probe: status="
              << response.status_code() << " model=" << response.model()
              << " http_status=" << response.http_status() << std::endl;
    return response.status_code() == nexusim::ai::AI_STATUS_OK ? 0 : 4;
}
