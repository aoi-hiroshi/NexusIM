#include "mprpcapplication.h"
#include "rpcprovider.h"
#include "userserviceimpl.hpp"

#include <iostream>

int main(int argc, char **argv)
{
    MprpcApplication::Init(argc, argv);
    nexusim::UserServiceImpl service;
    RpcProvider provider;
    provider.NotifyService(&service);
    if (!provider.Run())
    {
        std::cerr << "UserService failed to start" << std::endl;
        return 1;
    }
    return 0;
}
