# NexusIM

NexusIM 是基于施磊课程 ChatServer 与 MPRPC 示例代码演进的 C++17 分布式即时通信项目。

当前仓库已完成并实机验证 **阶段 2：Redis Pub/Sub + Nginx 双节点集群**。
阶段 3 的 MPRPC 加固和独立 UserService 源码已经完成，Windows 可移植测试已通过，
尚待 Ubuntu 目标机执行 Protobuf/Muduo/ZooKeeper/MySQL 的真实编译与联调。
Qt UI 和 AIService 尚未开发；完整 V1 仍不具备发布条件。

## 当前目录

- `chatserver/`：基于原 Muduo 课程工程渐进改造的 ChatServer 与 CLI 客户端。
- `mprpc/`：在原课程 MPRPC 核心上增加双向分帧、总 Deadline、结构化错误和 Provider 有界执行器。
- `proto/`：ChatServer 与 UserService 共享的 Protobuf 业务接口。
- `services/user_service/`：独立 UserService 进程，负责 Login、Register、QueryUser。
- `docs/phase0-baseline.md`：阶段 0 环境与基线记录。
- `docs/concurrency-presence-design.md`：异步 RPC、连接顺序和 Redis Presence 的强制设计规则。
- `docs/linux-build-checklist.md`：后续 Linux 编译与依赖核查命令。
- `docs/stage1-implementation.md`：阶段 1 设计、实际测试结果与 Linux 验收步骤。
- `docs/stage2-cluster.md`：阶段 2 路由、部署、可靠性边界与可复现验收步骤。
- `docs/stage3-mprpc-user-service.md`：阶段 3 RPC 协议、线程模型、构建、启动和验收步骤。
- `docs/v1-test-report.md`：当前 V1 测试结论、真实证据、未完成项和发布判定。
- `deploy/`：双实例环境模板、Redis 模板、Nginx stream 配置与启动脚本。

## V1 边界

V1 计划实现 TCP/RPC 分帧、RPC 超时、EventLoop 非阻塞、连接身份校验、安全数据库访问、双 ChatServer + Redis + Nginx、独立 UserService，以及后续的 Qt 5.12.8 客户端和基础 AIService。

V1 不承诺 ACK 可靠投递、多设备同步、复杂历史、RPC 多实例治理、流式 AI、RAG 或 MCP。

如果 V1 暂不启用 TLS，只允许部署在隔离测试环境，不得开放公网。

## 构建状态

阶段 1/2 回归及阶段 3 RPC 帧测试已在 VS2019 下通过；Linux 目标机已完成旧基线 ChatServer 编译、双节点
Redis Pub/Sub、Nginx 3/3 分流、Redis 故障回退和节点崩溃 TTL 验证。真实证据、
尚未执行项及非生产边界见 `docs/v1-test-report.md`。阶段 3 新增 Linux 代码不得在
目标机验证前描述为编译或集成通过。
