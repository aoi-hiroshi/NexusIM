# NexusIM

NexusIM 是基于施磊课程 ChatServer 与 MPRPC 示例代码演进的 C++17 分布式即时通信项目。

当前仓库已完成 **阶段 1：协议、安全与并发基线**。Qt UI、UserService RPC 拆分和 AIService 尚未开发。

## 当前目录

- `chatserver/`：基于原 Muduo 课程工程渐进改造的 ChatServer 与 CLI 客户端。
- `mprpc/`：原始 MPRPC 课程工程的源码快照，已排除历史二进制与构建缓存。
- `docs/phase0-baseline.md`：阶段 0 环境与基线记录。
- `docs/concurrency-presence-design.md`：异步 RPC、连接顺序和 Redis Presence 的强制设计规则。
- `docs/linux-build-checklist.md`：后续 Linux 编译与依赖核查命令。
- `docs/stage1-implementation.md`：阶段 1 设计、实际测试结果与 Linux 验收步骤。

## V1 边界

V1 计划实现 TCP/RPC 分帧、RPC 超时、EventLoop 非阻塞、连接身份校验、安全数据库访问、双 ChatServer + Redis + Nginx、独立 UserService，以及后续的 Qt 5.12.8 客户端和基础 AIService。

V1 不承诺 ACK 可靠投递、多设备同步、复杂历史、RPC 多实例治理、流式 AI、RAG 或 MCP。

如果 V1 暂不启用 TLS，只允许部署在隔离测试环境，不得开放公网。

## 构建状态

阶段 1 的平台无关核心测试已在 VS2019 下真实通过；本机没有 WSL/Muduo/MySQL/hiredis Linux 环境，Linux 服务端尚未在本机链接。真实边界和目标机命令见 `docs/stage1-implementation.md`。
