#include "userserviceimpl.hpp"

#include "server/security/passwordhasher.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <string>
#include <thread>

namespace
{

class DoneGuard
{
public:
    explicit DoneGuard(google::protobuf::Closure *done) : done_(done) {}
    ~DoneGuard()
    {
        if (done_ != nullptr) done_->Run();
    }

private:
    google::protobuf::Closure *done_;
};

void setResult(nexusim::user::UserResult *result,
               nexusim::user::UserErrorCode code,
               const std::string &message)
{
    result->set_code(code);
    result->set_message(message);
}

int environmentInt(const char *name, int fallback)
{
    const char *value = std::getenv(name);
    if (value == nullptr) return fallback;
    try
    {
        return std::stoi(value);
    }
    catch (...)
    {
        return fallback;
    }
}

void maybeDelayLogin(int userId)
{
    const int delayedUser = environmentInt("USER_SERVICE_TEST_DELAY_USER_ID", -1);
    const int delayMs = environmentInt("USER_SERVICE_TEST_DELAY_MS", 0);
    if (userId == delayedUser && delayMs > 0 && delayMs <= 30000)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
}

void fillPublicUser(nexusim::user::PublicUser *destination, const User &source)
{
    destination->set_user_id(source.getId());
    destination->set_username(source.getName());
}

} // namespace

namespace nexusim
{

void UserServiceImpl::Login(google::protobuf::RpcController *controller,
                            const user::LoginRequest *request,
                            user::LoginResponse *response,
                            google::protobuf::Closure *done)
{
    DoneGuard doneGuard(done);
    if (response == nullptr)
    {
        if (controller != nullptr) controller->SetFailed("Login response is null");
        return;
    }
    if (request == nullptr || request->user_id() <= 0 ||
        request->password().empty() || request->password().size() > 256)
    {
        setResult(response->mutable_result(), user::USER_ERROR_INVALID_ARGUMENT,
                  "invalid user id or password");
        return;
    }

    maybeDelayLogin(request->user_id());
    User record;
    const auto queryStatus = userModel_.queryById(request->user_id(), record);
    if (queryStatus == UserQueryStatus::Error)
    {
        setResult(response->mutable_result(), user::USER_ERROR_DATABASE,
                  "user database query failed");
        return;
    }
    if (queryStatus == UserQueryStatus::NotFound)
    {
        setResult(response->mutable_result(), user::USER_ERROR_INVALID_CREDENTIALS,
                  "id or password is invalid");
        return;
    }

    bool passwordMatches = PasswordHasher::verify(request->password(), record.getPwd());
    if (!passwordMatches && !PasswordHasher::isEncoded(record.getPwd()) &&
        record.getPwd() == request->password())
    {
        try
        {
            const bool migrated = userModel_.updatePassword(
                record.getId(), PasswordHasher::hash(request->password()));
            if (!migrated)
            {
                setResult(response->mutable_result(), user::USER_ERROR_DATABASE,
                          "legacy password migration update failed");
                return;
            }
            passwordMatches = true;
        }
        catch (const std::exception &)
        {
            setResult(response->mutable_result(), user::USER_ERROR_PASSWORD_HASH,
                      "legacy password migration failed");
            return;
        }
    }
    if (!passwordMatches)
    {
        setResult(response->mutable_result(), user::USER_ERROR_INVALID_CREDENTIALS,
                  "id or password is invalid");
        return;
    }

    response->set_authenticated(true);
    fillPublicUser(response->mutable_user(), record);
    setResult(response->mutable_result(), user::USER_ERROR_OK, "");
}

void UserServiceImpl::Register(google::protobuf::RpcController *controller,
                               const user::RegisterRequest *request,
                               user::RegisterResponse *response,
                               google::protobuf::Closure *done)
{
    DoneGuard doneGuard(done);
    if (response == nullptr)
    {
        if (controller != nullptr) controller->SetFailed("Register response is null");
        return;
    }
    if (request == nullptr || request->username().empty() ||
        request->username().size() > 50 || request->password().size() < 8 ||
        request->password().size() > 128)
    {
        setResult(response->mutable_result(), user::USER_ERROR_INVALID_ARGUMENT,
                  "username must contain 1-50 bytes and password 8-128 bytes");
        return;
    }

    User record;
    record.setName(request->username());
    try
    {
        record.setPwd(PasswordHasher::hash(request->password()));
    }
    catch (const std::exception &)
    {
        setResult(response->mutable_result(), user::USER_ERROR_PASSWORD_HASH,
                  "password hashing failed");
        return;
    }
    if (!userModel_.insert(record))
    {
        setResult(response->mutable_result(), user::USER_ERROR_DATABASE,
                  "registration database operation failed");
        return;
    }

    response->set_registered(true);
    fillPublicUser(response->mutable_user(), record);
    setResult(response->mutable_result(), user::USER_ERROR_OK, "");
}

void UserServiceImpl::QueryUser(google::protobuf::RpcController *controller,
                                const user::QueryUserRequest *request,
                                user::QueryUserResponse *response,
                                google::protobuf::Closure *done)
{
    DoneGuard doneGuard(done);
    if (response == nullptr)
    {
        if (controller != nullptr) controller->SetFailed("QueryUser response is null");
        return;
    }
    if (request == nullptr || request->user_id() <= 0)
    {
        setResult(response->mutable_result(), user::USER_ERROR_INVALID_ARGUMENT,
                  "invalid user id");
        return;
    }

    User record;
    const auto queryStatus = userModel_.queryById(request->user_id(), record);
    if (queryStatus == UserQueryStatus::Error)
    {
        setResult(response->mutable_result(), user::USER_ERROR_DATABASE,
                  "user database query failed");
        return;
    }
    if (queryStatus == UserQueryStatus::NotFound)
    {
        setResult(response->mutable_result(), user::USER_ERROR_NOT_FOUND,
                  "user does not exist");
        return;
    }

    response->set_found(true);
    fillPublicUser(response->mutable_user(), record);
    setResult(response->mutable_result(), user::USER_ERROR_OK, "");
}

} // namespace nexusim
