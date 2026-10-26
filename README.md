# NexusIM

NexusIM 是基于施磊课程 ChatServer 与 MPRPC 示例代码演进的 C++17 分布式即时通信项目。

当前仓库已完成并实机验证 **阶段 2：Redis Pub/Sub + Nginx 双节点集群** 和
**阶段 3：MPRPC 加固与独立 UserService 拆分**。Stage 3 已在 Ubuntu 目标机完成
Protobuf/Muduo/ZooKeeper/MySQL 真实构建、RPC/故障注入及 Stage 1/2 最终回归。
Stage 4 已新增 Qt 5.12.8 桌面客户端，用户已在测试 PC 完成双端联调。
Stage 5 已新增独立 AIService、DeepSeek V4 Pro HTTPS 适配、ChatServer AI RPC
隔离执行器与 Qt AI 对话页，并已完成用户提供的 Linux/真实 API 回归。
Stage 6 已实现双向好友、群组管理、双节点分布式群聊、离线群消息和 Qt 社交页面；
Windows 构建与单元测试、Linux 双节点业务探针均已通过。
Stage 7 已补齐多阶段 Linux 服务镜像、Docker Compose 完整拓扑、独立 MySQL
Named Volume 初始化、Docker DNS RPC 注册、非破坏性运维脚本和 Windows Release
发布流水线；Ubuntu 26.04 上 8 容器健康、故障注入、持久化及真实 AI 调用均已验证。
Stage 6 的双 Qt 图形客户端联调仍待单独验收。

## 当前目录

- `chatserver/`：基于原 Muduo 课程工程渐进改造的 ChatServer 与 CLI 客户端。
- `mprpc/`：在原课程 MPRPC 核心上增加双向分帧、总 Deadline、结构化错误和 Provider 有界执行器。
- `proto/`：ChatServer 与 UserService 共享的 Protobuf 业务接口。
- `services/user_service/`：独立 UserService 进程，负责 Login、Register、QueryUser。
- `services/ai_service/`：独立 AIService，负责多轮上下文和 DeepSeek HTTPS 调用。
- `client/qt/`：独立 Qt 5.12.8 Windows 客户端、QSS、QtTest 与打包脚本。
- `docs/phase0-baseline.md`：阶段 0 环境与基线记录。
- `docs/concurrency-presence-design.md`：异步 RPC、连接顺序和 Redis Presence 的强制设计规则。
- `docs/linux-build-checklist.md`：后续 Linux 编译与依赖核查命令。
- `docs/stage1-implementation.md`：阶段 1 设计、实际测试结果与 Linux 验收步骤。
- `docs/stage2-cluster.md`：阶段 2 路由、部署、可靠性边界与可复现验收步骤。
- `docs/stage3-mprpc-user-service.md`：阶段 3 RPC 协议、线程模型、构建、启动和验收步骤。
- `docs/stage3-test-report.md`：阶段 3 Linux 实机、故障注入及最终回归的真实测试报告。
- `docs/stage4-protocol-matrix.md`：客户端所适配的真实 JSON 协议与功能开放状态。
- `docs/stage4-qt-client.md`：Qt 客户端实现、构建、打包、测试和双端联调步骤。
- `docs/stage4-test-report.md`：Stage 4 Windows 实际编译、测试、打包、启动结果与未验证边界。
- `docs/stage5-implementation.md`：AI RPC、线程、生命周期、安全和 Qt 交互设计。
- `docs/stage5-linux-testing.md`：Ubuntu 构建、启动、Mock/真实 API 与双客户端验收命令。
- `docs/stage5-test-report.md`：Stage 5 实际 Windows 测试与 Linux 待验证边界。
- `docs/stage6-implementation.md`：好友、群组、Redis 群聊、SQL 与 Qt 设计。
- `docs/stage6-linux-testing.md`：Stage 6 Linux 数据迁移、构建、启动和验收命令。
- `docs/stage6-test-report.md`：Stage 6 Windows 与 Linux 实机测试结果、未验证项。
- `compose.yaml`：MySQL、Redis、ZooKeeper、UserService、AIService、双 ChatServer 和 Nginx 的完整编排。
- `deploy/docker/`：多阶段后端镜像、容器数据库 Schema、Nginx Stream 配置和环境模板。
- `docs/stage7-deployment.md`：Stage 7 构建、部署、发布、安全边界与故障排查。
- `docs/stage7-acceptance.md`：Ubuntu/Windows 最终验收顺序和证据清单。
- `docs/stage7-test-report.md`：Stage 7 容器构建、8 服务联调、故障和持久化测试结果。
- `THIRD_PARTY_NOTICES.md`：依赖版本、来源、许可证提示及课程源码署名边界。
- `docs/v1-test-report.md`：当前 V1 测试结论、真实证据、未完成项和发布判定。
- `deploy/`：双实例环境模板、Redis 模板、Nginx stream 配置与启动脚本。

## V1 边界

V1 已实现 TCP/RPC 分帧、RPC 超时、EventLoop 非阻塞、连接身份校验、安全数据库访问、双 ChatServer + Redis + Nginx、独立 UserService、Qt 5.12.8 客户端、基础 AIService，以及好友、群组与分布式群聊。

V1 不承诺 ACK 可靠投递、多设备同步、复杂历史、RPC 多实例治理、流式 AI、RAG 或 MCP。

如果 V1 暂不启用 TLS，只允许部署在隔离测试环境，不得开放公网。

## Docker Compose 快速开始

```bash
cp deploy/docker/.env.example .env
chmod 600 .env
# 编辑 .env，替换三个密码占位符；DeepSeek Key 可暂时留空
deploy/scripts/compose-up.sh
deploy/scripts/compose-status.sh
```

Windows Qt 客户端连接 Ubuntu 虚拟机的 Nginx Stream 端口，例如
`192.168.73.128:8000`。普通停止使用
`deploy/scripts/compose-down.sh`，该脚本不会删除 Named Volume。
完整步骤见 `docs/stage7-deployment.md`。

## 构建状态

阶段 1/2 回归及阶段 3 RPC 帧测试已在 VS2019 下通过；Linux 目标机已完成双节点
Redis Pub/Sub、Nginx 3/3 分流、Redis 故障回退、节点崩溃 TTL、独立 UserService、
真实 RPC Deadline、慢 RPC 隔离和断线生命周期验证。真实证据与非生产边界见
`docs/v1-test-report.md` 和 `docs/stage3-test-report.md`。
