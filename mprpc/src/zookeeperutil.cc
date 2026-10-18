#include "zookeeperutil.h"

#include "mprpcapplication.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{

int positiveInt(const std::string &value, int fallback)
{
    try
    {
        const int parsed = std::stoi(value);
        return parsed > 0 ? parsed : fallback;
    }
    catch (...)
    {
        return fallback;
    }
}

void globalWatcher(zhandle_t *, int type, int state, const char *, void *context)
{
    auto *client = static_cast<ZkClient *>(context);
    if (client != nullptr) client->HandleWatcherEvent(type, state);
}

std::string zkError(int code)
{
    return std::string(zerror(code));
}

struct GetDataState
{
    std::mutex mutex;
    std::condition_variable changed;
    bool done{false};
    int result{ZSYSTEMERROR};
    std::string value;
};

void getDataCompletion(int result, const char *value, int valueLength,
                       const Stat *, const void *context)
{
    std::unique_ptr<std::shared_ptr<GetDataState>> holder(
        static_cast<std::shared_ptr<GetDataState> *>(
            const_cast<void *>(context)));
    const auto state = *holder;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->result = result;
        if (result == ZOK && value != nullptr && valueLength >= 0)
            state->value.assign(value, static_cast<std::size_t>(valueLength));
        state->done = true;
    }
    state->changed.notify_one();
}

} // namespace

ZkClient::ZkClient() : m_zhandle(nullptr) {}

ZkClient::~ZkClient()
{
    if (m_zhandle != nullptr)
    {
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
    }
}

bool ZkClient::Start(std::chrono::milliseconds timeout, std::string *error)
{
    const auto &config = MprpcApplication::GetInstance().GetConfig();
    const std::string host = config.LoadOr("zookeeperip", "127.0.0.1");
    const std::string port = config.LoadOr("zookeeperport", "2181");
    const std::string connection = host + ":" + port;
    const int configuredSession = positiveInt(
        config.LoadOr("zksessiontimeout_ms", "3000"), 3000);
    const int timeoutMs = static_cast<int>(
        std::max<std::int64_t>(1, timeout.count()));
    const int sessionTimeout =
        std::max(1000, std::min(configuredSession, timeoutMs));

    {
        std::lock_guard<std::mutex> lock(mutex_);
        connected_ = false;
    }
    m_zhandle = zookeeper_init(connection.c_str(), globalWatcher,
                               sessionTimeout, nullptr, this, 0);
    if (m_zhandle == nullptr)
    {
        if (error != nullptr) *error = "zookeeper_init failed";
        return false;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    if (!stateChanged_.wait_for(lock, timeout, [this] { return connected_; }))
    {
        lock.unlock();
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
        if (error != nullptr) *error = "ZooKeeper connection timed out";
        return false;
    }
    return true;
}

void ZkClient::HandleWatcherEvent(int type, int state)
{
    if (type != ZOO_SESSION_EVENT) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connected_ = state == ZOO_CONNECTED_STATE;
    }
    stateChanged_.notify_all();
}

bool ZkClient::Connected() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

bool ZkClient::Create(const char *path, const char *data, int datalen,
                      int state, std::string *error)
{
    if (m_zhandle == nullptr || !Connected())
    {
        if (error != nullptr) *error = "ZooKeeper is not connected";
        return false;
    }

    const int exists = zoo_exists(m_zhandle, path, 0, nullptr);
    if (exists == ZOK)
    {
        if (state == 0) return true;
        if (error != nullptr) *error = "ephemeral znode already exists";
        return false;
    }
    if (exists != ZNONODE)
    {
        if (error != nullptr) *error = "exists failed: " + zkError(exists);
        return false;
    }

    std::vector<char> createdPath(512, '\0');
    const int result = zoo_create(
        m_zhandle, path, data, datalen, &ZOO_OPEN_ACL_UNSAFE, state,
        createdPath.data(), static_cast<int>(createdPath.size()));
    if (result != ZOK)
    {
        if (error != nullptr) *error = "create failed: " + zkError(result);
        return false;
    }
    return true;
}

bool ZkClient::GetData(const char *path, std::string &value,
                       std::string *error)
{
    return GetData(path, value, std::chrono::milliseconds(3000), error);
}

bool ZkClient::GetData(const char *path, std::string &value,
                       std::chrono::milliseconds timeout,
                       std::string *error)
{
    value.clear();
    if (m_zhandle == nullptr || !Connected())
    {
        if (error != nullptr) *error = "ZooKeeper is not connected";
        return false;
    }

    auto state = std::make_shared<GetDataState>();
    auto *holder = new std::shared_ptr<GetDataState>(state);
    const int scheduled = zoo_aget(m_zhandle, path, 0, getDataCompletion,
                                   holder);
    if (scheduled != ZOK)
    {
        delete holder;
        if (error != nullptr) *error = "get scheduling failed: " + zkError(scheduled);
        return false;
    }

    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->changed.wait_for(lock, timeout, [&state] { return state->done; }))
    {
        if (error != nullptr) *error = "ZooKeeper query timed out";
        return false;
    }
    if (state->result != ZOK)
    {
        if (error != nullptr) *error = "get failed: " + zkError(state->result);
        return false;
    }
    value = state->value;
    return true;
}

std::string ZkClient::GetData(const char *path)
{
    std::string value;
    GetData(path, value, nullptr);
    return value;
}
