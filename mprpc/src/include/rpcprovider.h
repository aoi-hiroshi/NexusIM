#pragma once
#include "boundedexecutor.h"
#include "rpcframecodec.h"
#include "google/protobuf/service.h"
#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpConnection.h>
#include <string>
#include <functional>
#include <google/protobuf/descriptor.h>
#include <unordered_map>
#include <memory>

// 框架提供的专门发布rpc服务的网络对象类
class RpcProvider
{
public:
    RpcProvider();
    // 这里是框架提供给外部使用的，可以发布rpc方法的函数接口
    void NotifyService(google::protobuf::Service *service);

    // 启动rpc服务节点，开始提供rpc远程网络调用服务
    bool Run();

private:
    // 组合EventLoop
    muduo::net::EventLoop m_eventLoop;

    // service服务类型信息
    struct ServiceInfo
    {
        google::protobuf::Service *m_service; // 保存服务对象
        std::unordered_map<std::string, const google::protobuf::MethodDescriptor*> m_methodMap; // 保存服务方法
    };
    // 存储注册成功的服务对象和其服务方法的所有信息
    std::unordered_map<std::string, ServiceInfo> m_serviceMap;
    mprpc::BoundedExecutor executor_;
    mprpc::RpcFrameCodec codec_;

    struct CallContext;

    // 新的socket连接回调
    void OnConnection(const muduo::net::TcpConnectionPtr&);
    // 已建立连接用户的读写事件回调
    void OnMessage(const muduo::net::TcpConnectionPtr&, muduo::net::Buffer*, muduo::Timestamp);
    void DispatchFrame(const muduo::net::TcpConnectionPtr&, std::string);
    void SendRpcResponse(const std::shared_ptr<CallContext>&);
    void SendError(const muduo::net::TcpConnectionPtr&, const std::string &requestId,
                   int statusCode, const std::string &message, bool closeAfter);
};
