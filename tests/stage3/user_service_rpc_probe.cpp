#include "mprpcapplication.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "user_service.pb.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{

using Clock = MprpcController::Clock;

std::string requiredEnvironment(const char *name)
{
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0')
    {
        std::cerr << name << " is required" << std::endl;
        std::exit(2);
    }
    return value;
}

void requireCall(const MprpcController &controller, const char *method)
{
    if (controller.Failed())
    {
        std::cerr << method << " RPC failed: " << controller.ErrorText()
                  << std::endl;
        std::exit(1);
    }
}

} // namespace

int main(int argc, char **argv)
{
    MprpcApplication::Init(argc, argv);
    const std::string password =
        requiredEnvironment("NEXUSIM_RPC_PROBE_PASSWORD");
    const auto suffix = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const std::string username = "stage3_rpc_" + std::to_string(suffix);

    MprpcChannel channel;
    nexusim::user::UserService_Stub stub(&channel);

    nexusim::user::RegisterRequest registerRequest;
    registerRequest.set_username(username);
    registerRequest.set_password(password);
    nexusim::user::RegisterResponse registerResponse;
    MprpcController registerController;
    registerController.SetDeadline(Clock::now() + std::chrono::seconds(3));
    stub.Register(&registerController, &registerRequest, &registerResponse,
                  nullptr);
    requireCall(registerController, "Register");
    if (!registerResponse.registered())
    {
        std::cerr << "Register business failure: "
                  << registerResponse.result().message() << std::endl;
        return 1;
    }
    const int userId = registerResponse.user().user_id();

    nexusim::user::LoginRequest loginRequest;
    loginRequest.set_user_id(userId);
    loginRequest.set_password(password);
    nexusim::user::LoginResponse loginResponse;
    MprpcController loginController;
    loginController.SetDeadline(Clock::now() + std::chrono::seconds(3));
    stub.Login(&loginController, &loginRequest, &loginResponse, nullptr);
    requireCall(loginController, "Login");
    if (!loginResponse.authenticated() ||
        loginResponse.user().username() != username)
    {
        std::cerr << "Login response mismatch" << std::endl;
        return 1;
    }

    nexusim::user::LoginRequest rejectedRequest;
    rejectedRequest.set_user_id(userId);
    rejectedRequest.set_password(password + "-wrong");
    nexusim::user::LoginResponse rejectedResponse;
    MprpcController rejectedController;
    rejectedController.SetDeadline(Clock::now() + std::chrono::seconds(3));
    stub.Login(&rejectedController, &rejectedRequest, &rejectedResponse, nullptr);
    requireCall(rejectedController, "Login(wrong password)");
    if (rejectedResponse.authenticated() ||
        rejectedResponse.result().code() !=
            nexusim::user::USER_ERROR_INVALID_CREDENTIALS)
    {
        std::cerr << "wrong password was not rejected" << std::endl;
        return 1;
    }

    nexusim::user::QueryUserRequest queryRequest;
    queryRequest.set_user_id(userId);
    nexusim::user::QueryUserResponse queryResponse;
    MprpcController queryController;
    queryController.SetDeadline(Clock::now() + std::chrono::seconds(3));
    stub.QueryUser(&queryController, &queryRequest, &queryResponse, nullptr);
    requireCall(queryController, "QueryUser");
    if (!queryResponse.found() || queryResponse.user().username() != username)
    {
        std::cerr << "QueryUser response mismatch" << std::endl;
        return 1;
    }

    std::cout << "stage3_user_service_rpc_probe: PASS user_id=" << userId
              << std::endl;
    return 0;
}
