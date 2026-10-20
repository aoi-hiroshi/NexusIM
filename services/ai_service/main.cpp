#include "ai_service_impl.hpp"
#include "mprpcapplication.h"
#include "rpcprovider.h"

#include <iostream>

int main(int argc, char **argv)
{
    MprpcApplication::Init(argc, argv);
    nexusim::ai_service::AIServiceImpl service;
    RpcProvider provider;
    provider.NotifyService(&service);
    if (!provider.Run())
    {
        std::cerr << "AIService failed to start" << std::endl;
        return 1;
    }
    return 0;
}
