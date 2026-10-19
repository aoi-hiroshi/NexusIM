#include "mprpcconfig.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

namespace
{
const std::unordered_map<std::string, std::string> kEnvironmentNames{
    {"rpcserverip", "MPRPC_RPC_SERVER_IP"},
    {"rpcserverport", "MPRPC_RPC_SERVER_PORT"},
    {"zookeeperip", "MPRPC_ZOOKEEPER_IP"},
    {"zookeeperport", "MPRPC_ZOOKEEPER_PORT"},
    {"rpcdeadline_ms", "MPRPC_DEADLINE_MS"},
    {"zkconnecttimeout_ms", "MPRPC_ZK_CONNECT_TIMEOUT_MS"},
    {"zksessiontimeout_ms", "MPRPC_ZK_SESSION_TIMEOUT_MS"},
    {"zkregistrationtimeout_ms", "MPRPC_ZK_REGISTRATION_TIMEOUT_MS"},
    {"rpcmaxframe_bytes", "MPRPC_MAX_FRAME_BYTES"},
    {"providerthreads", "MPRPC_PROVIDER_THREADS"},
    {"providerqueue", "MPRPC_PROVIDER_QUEUE"},
    {"provideriothreads", "MPRPC_PROVIDER_IO_THREADS"},
};
}

// 负责解析加载配置文件
bool MprpcConfig::LoadConfigFile(const char *config_file)
{
    FILE *pf = fopen(config_file, "r");
    if (nullptr == pf)
    {
        std::cerr << config_file << " does not exist" << std::endl;
        return false;
    }

    // 1.注释   2.正确的配置项 =    3.去掉开头的多余的空格 
    while(!feof(pf))
    {
        char buf[512] = {0};
        fgets(buf, 512, pf);

        // 去掉字符串前面多余的空格
        std::string read_buf(buf);
        Trim(read_buf);

        // 判断#的注释
        if (read_buf.empty() || read_buf[0] == '#')
        {
            continue;
        }

        // 解析配置项
        int idx = read_buf.find('=');
        if (idx == -1)
        {
            // 配置项不合法
            continue;
        }

        std::string key;
        std::string value;
        key = read_buf.substr(0, idx);
        Trim(key);
        // rpcserverip=127.0.0.1\n
        int endidx = read_buf.find('\n', idx);
        value = read_buf.substr(idx+1, endidx-idx-1);
        Trim(value);
        m_configMap.insert({key, value});
    }

    fclose(pf);
    return true;
}

// 查询配置项信息
std::string MprpcConfig::Load(const std::string &key) const
{
    auto it = m_configMap.find(key);
    if (it != m_configMap.end())
    {
        return it->second;
    }
    const auto environmentName = kEnvironmentNames.find(key);
    if (environmentName == kEnvironmentNames.end()) return "";
    const char *value = std::getenv(environmentName->second.c_str());
    return value == nullptr ? std::string() : std::string(value);
}

std::string MprpcConfig::LoadOr(const std::string &key,
                                const std::string &fallback) const
{
    const std::string value = Load(key);
    return value.empty() ? fallback : value;
}

// 去掉字符串前后的空格
void MprpcConfig::Trim(std::string &src_buf)
{
    int idx = src_buf.find_first_not_of(' ');
    if (idx != -1)
    {
        // 说明字符串前面有空格
        src_buf = src_buf.substr(idx, src_buf.size()-idx);
    }
    // 去掉字符串后面多余的空格
    idx = src_buf.find_last_not_of(' ');
    if (idx != -1)
    {
        // 说明字符串后面有空格
        src_buf = src_buf.substr(0, idx+1);
    }
}
