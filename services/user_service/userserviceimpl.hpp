#pragma once

#include "user_service.pb.h"
#include "usermodel.hpp"

namespace nexusim
{

class UserServiceImpl final : public user::UserService
{
public:
    void Login(google::protobuf::RpcController *controller,
               const user::LoginRequest *request,
               user::LoginResponse *response,
               google::protobuf::Closure *done) override;

    void Register(google::protobuf::RpcController *controller,
                  const user::RegisterRequest *request,
                  user::RegisterResponse *response,
                  google::protobuf::Closure *done) override;

    void QueryUser(google::protobuf::RpcController *controller,
                   const user::QueryUserRequest *request,
                   user::QueryUserResponse *response,
                   google::protobuf::Closure *done) override;

private:
    UserModel userModel_;
};

} // namespace nexusim
