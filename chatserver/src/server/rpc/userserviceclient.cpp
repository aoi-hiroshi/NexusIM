#include "server/rpc/userserviceclient.hpp"

#include "mprpcchannel.h"
#include "user_service.pb.h"

namespace
{

nexusim::RpcCallStatus callStatus(const MprpcController &controller)
{
    if (controller.Failed())
    {
        return {false, controller.ErrorCode(), controller.ErrorText()};
    }
    return {true, mprpc::RpcErrorCode::Ok, ""};
}

} // namespace

namespace nexusim
{

LoginRpcResult UserServiceClient::login(int userId, const std::string &password,
                                        Deadline deadline) const
{
    MprpcChannel channel;
    user::UserService_Stub stub(&channel);
    user::LoginRequest request;
    request.set_user_id(userId);
    request.set_password(password);
    user::LoginResponse response;
    MprpcController controller;
    controller.SetDeadline(deadline);
    stub.Login(&controller, &request, &response, nullptr);

    LoginRpcResult result;
    result.call = callStatus(controller);
    if (!result.call.ok) return result;
    result.authenticated = response.authenticated();
    result.userError = static_cast<int>(response.result().code());
    result.userMessage = response.result().message();
    if (response.has_user())
    {
        result.userId = response.user().user_id();
        result.username = response.user().username();
    }
    return result;
}

RegisterRpcResult UserServiceClient::registerUser(
    const std::string &username, const std::string &password,
    Deadline deadline) const
{
    MprpcChannel channel;
    user::UserService_Stub stub(&channel);
    user::RegisterRequest request;
    request.set_username(username);
    request.set_password(password);
    user::RegisterResponse response;
    MprpcController controller;
    controller.SetDeadline(deadline);
    stub.Register(&controller, &request, &response, nullptr);

    RegisterRpcResult result;
    result.call = callStatus(controller);
    if (!result.call.ok) return result;
    result.registered = response.registered();
    result.userError = static_cast<int>(response.result().code());
    result.userMessage = response.result().message();
    if (response.has_user())
    {
        result.userId = response.user().user_id();
        result.username = response.user().username();
    }
    return result;
}

QueryUserRpcResult UserServiceClient::queryUser(int userId,
                                                Deadline deadline) const
{
    MprpcChannel channel;
    user::UserService_Stub stub(&channel);
    user::QueryUserRequest request;
    request.set_user_id(userId);
    user::QueryUserResponse response;
    MprpcController controller;
    controller.SetDeadline(deadline);
    stub.QueryUser(&controller, &request, &response, nullptr);

    QueryUserRpcResult result;
    result.call = callStatus(controller);
    if (!result.call.ok) return result;
    result.found = response.found();
    result.userError = static_cast<int>(response.result().code());
    result.userMessage = response.result().message();
    if (response.has_user())
    {
        result.userId = response.user().user_id();
        result.username = response.user().username();
    }
    return result;
}

} // namespace nexusim
