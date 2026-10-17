#include "server/chatserver.hpp"

#include <muduo/base/Logging.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
std::string environmentOr(const char *name, const char *fallback)
{
    const char *value = std::getenv(name);
    return value == nullptr || *value == '\0' ? std::string(fallback) : std::string(value);
}

std::uint16_t parsePort(const std::string &value)
{
    const unsigned long parsed = std::stoul(value);
    if (parsed == 0 || parsed > 65535) throw std::out_of_range("port");
    return static_cast<std::uint16_t>(parsed);
}
} // namespace

int main(int argc, char **argv)
{
    std::string ip = environmentOr("CHAT_LISTEN_IP", "127.0.0.1");
    std::string portText = environmentOr("CHAT_LISTEN_PORT", "6000");
    if (argc == 3)
    {
        ip = argv[1];
        portText = argv[2];
    }
    else if (argc != 1)
    {
        std::cerr << "usage: ChatServer [listen_ip listen_port]" << std::endl;
        return 2;
    }

    std::uint16_t port = 0;
    try
    {
        port = parsePort(portText);
    }
    catch (...)
    {
        std::cerr << "invalid listen port: " << portText << std::endl;
        return 2;
    }

    const std::string nodeId = environmentOr("CHAT_NODE_ID", "chatserver-local");
    LOG_INFO << "starting NexusIM node=" << nodeId << " listen=" << ip << ':' << port;
    muduo::net::EventLoop loop;
    muduo::net::InetAddress address(ip, port);
    ChatServer server(&loop, address, "NexusIM-" + nodeId);
    server.start();
    loop.loop();
    return 0;
}
