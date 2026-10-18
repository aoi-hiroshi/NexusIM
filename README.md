# NexusIM

NexusIM 是基于施磊课程 ChatServer 与 MPRPC 示例代码演进的 C++17 分布式即时通信项目。

当前仓库已完成并实机验证 **阶段 2：Redis Pub/Sub + Nginx 双节点集群**。
Qt UI、UserService RPC 拆分和 AIService 尚未开发；完整 V1 仍不具备发布条件。

## 当前目录

- `chatserver/`：基于原 Muduo 课程工程渐进改造的 ChatServer 与 CLI 客户端。
- `mprpc/`：原始 MPRPC 课程工程的源码快照，已排除历史二进制与构建缓存。
- `docs/phase0-baseline.md`：阶段 0 环境与基线记录。
- `docs/concurrency-presence-design.md`：异步 RPC、连接顺序和 Redis Presence 的强制设计规则。
- `docs/linux-build-checklist.md`：后续 Linux 编译与依赖核查命令。
- `docs/stage1-implementation.md`：阶段 1 设计、实际测试结果与 Linux 验收步骤。
- `docs/stage2-cluster.md`：阶段 2 路由、部署、可靠性边界与可复现验收步骤。
- `docs/v1-test-report.md`：当前 V1 测试结论、真实证据、未完成项和发布判定。
- `deploy/`：双实例环境模板、Redis 模板、Nginx stream 配置与启动脚本。

## V1 边界

V1 计划实现 TCP/RPC 分帧、RPC 超时、EventLoop 非阻塞、连接身份校验、安全数据库访问、双 ChatServer + Redis + Nginx、独立 UserService，以及后续的 Qt 5.12.8 客户端和基础 AIService。

V1 不承诺 ACK 可靠投递、多设备同步、复杂历史、RPC 多实例治理、流式 AI、RAG 或 MCP。

如果 V1 暂不启用 TLS，只允许部署在隔离测试环境，不得开放公网。

## 构建状态

平台无关核心测试在 VS2019 下通过；Linux 目标机已完成 ChatServer 编译、双节点
Redis Pub/Sub、Nginx 3/3 分流、Redis 故障回退和节点崩溃 TTL 验证。真实证据、
尚未执行项及非生产边界见 `docs/v1-test-report.md`。
