#pragma once

#include <zookeeper/zookeeper.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>

// 封装的zk客户端类
class ZkClient
{
public:
    ZkClient();
    ~ZkClient();
    // zkclient启动连接zkserver
    bool Start(std::chrono::milliseconds timeout, std::string *error = nullptr);
    // 在zkserver上根据指定的path创建znode节点
    bool Create(const char *path, const char *data, int datalen, int state=0,
                std::string *error = nullptr, int *resultCode = nullptr);
    // 根据参数指定的znode节点路径，或者znode节点的值
    bool GetData(const char *path, std::string &value,
                 std::string *error = nullptr);
    bool GetData(const char *path, std::string &value,
                 std::chrono::milliseconds timeout,
                 std::string *error = nullptr);
    std::string GetData(const char *path);
    bool Connected() const;
    void HandleWatcherEvent(int type, int state);
private:
    // zk的客户端句柄
    zhandle_t *m_zhandle;
    mutable std::mutex mutex_;
    std::condition_variable stateChanged_;
    bool connected_{false};
};
