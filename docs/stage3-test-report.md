# NexusIM Stage 3 测试报告

> 报告日期：2026-10-19（Asia/Shanghai）
>
> Linux 被测提交：`ba686aca0e143cc9dc7b7b1faa28636bbddd8acb`
>
> Stage 3 主功能提交：`a32b8d75228fecfce6e61f0da932b813e74f0cfc`

## 1. 验收结论

**Stage 3：MPRPC 框架加固与独立 UserService 拆分，已通过当前阶段验收。**

Windows 可移植测试和 Linux 实机测试均已执行。Linux 上完成了统一 CMake 构建、
UserService 注册与发现、真实 MySQL Register/Login/QueryUser RPC、RPC 传输分帧、
服务故障与 ZooKeeper 故障、真实 Deadline、慢 RPC 隔离、断线生命周期、快速重启，
以及 Stage 1/2 最终回归。

该结论只覆盖 Stage 3 约定范围，不代表完整 V1 已完成，也不表示系统已达到公网或生产
部署标准。Qt5 客户端、AIService 和完整 Docker Compose 仍属于后续阶段。

## 2. 被测范围

本轮覆盖：

1. MPRPC 请求和响应统一使用 4 字节大端长度前缀；
2. `RpcEnvelope`、请求 ID、结构化错误和总 Deadline；
3. ZooKeeper 服务注册与发现；
4. Provider 有界工作线程，阻塞 MySQL/RPC 不进入 Muduo EventLoop；
5. 独立 UserService 的 `Login`、`Register`、`QueryUser`；
6. ChatServer 经 MPRPC 调用 UserService；
7. PendingRequest 超时、断线取消和连接 lifecycle 隔离；
8. UserService 快速重启时旧 ZooKeeper 临时节点的有界等待；
9. Stage 1 TCP/会话安全与 Stage 2 双节点集群回归。

不在本轮范围：RPC 多实例负载均衡、自动重新注册、连接池、重试/熔断、TLS、Qt UI、
AIService、流式回复和可靠消息 ACK。

## 3. 实际环境

### 3.1 Windows 开发机

| 项目 | 实际信息 |
|---|---|
| 操作系统 | Windows 本地工作区 |
| 编译器 | Visual Studio 2019 / MSVC 19.29 |
| CMake | VS2019 内置 CMake 3.20.21032501 |
| 用途 | 平台无关单元测试、源码守卫、脚本语法检查 |
| 限制 | 不具备 Linux Muduo/ZooKeeper/MySQL 服务端联调环境 |

### 3.2 Linux 目标机

| 项目 | 实际信息 |
|---|---|
| 仓库路径 | `/home/aoi/NexusIM/NexusIM` |
| 编译器 | GCC 15.2.0 |
| CMake | 4.2 |
| Protobuf | 3.21.12 |
| OpenSSL | 3.5.5 |
| ZooKeeper C client | 3.9.5 |
| ZooKeeper | `127.0.0.1:2181`，systemd 服务实际运行 |
| MySQL | `:3306`，使用既有 `nexusim` Schema |
| Redis | `127.0.0.1:6379`，实际 `PING` 返回 `PONG` |
| UserService | `127.0.0.1:7001` |
| ChatServer | `127.0.0.1:6001`、`127.0.0.1:6002` |
| Nginx Stream | `0.0.0.0:8000` |

未取得 Muduo、MySQL Server 和 Redis Server 的完整版本输出，因此不推测版本。

## 4. 结果总表

| 编号 | 测试项 | 平台 | 结果 | 真实证据摘要 |
|---|---|---|---|---|
| S3-01 | 根 CMake 配置及目标编译 | Linux | 通过 | `ChatServer`、`UserService`、4 个 Stage 3 测试目标构建成功 |
| S3-02 | RPC FrameCodec 单元测试 | Linux | 通过 | `stage3_rpc_frame_tests` 通过 |
| S3-03 | RPC Protobuf 协议测试 | Linux | 通过 | `stage3_rpc_protocol_tests` 通过 |
| S3-04 | Stage 3 源码边界守卫 | Windows + Linux | 通过 | `stage3_source_guards: PASS` |
| S3-05 | ZooKeeper 三个方法注册 | Linux | 通过 | Login/Register/QueryUser 均返回 `127.0.0.1:7001` |
| S3-06 | 真实 Register/Login/QueryUser RPC | Linux + MySQL | 通过 | `stage3_user_service_rpc_probe: PASS user_id=27` |
| S3-07 | 错误密码拒绝 | Linux + MySQL | 通过 | 包含在 `stage3_user_service_rpc_probe` 断言中 |
| S3-08 | RPC 半包、粘包、非法 Envelope/长度 | Linux | 通过 | `stage3_rpc_transport_probe: PASS` |
| S3-09 | 新注册密码哈希存储 | Linux + MySQL | 通过 | 实查前缀 `pbkdf2_sha256$120000$...` |
| S3-10 | UserService 不可用 | Linux 故障注入 | 通过 | `USER_SERVICE_UNAVAILABLE`，单次观测 0.005 秒 |
| S3-11 | RPC 端点不可达 | Linux 故障注入 | 通过 | `RPC_UNAVAILABLE`，单次观测 0.004 秒 |
| S3-12 | ZooKeeper 不可用 | Linux 故障注入 | 通过 | `ZOOKEEPER_UNAVAILABLE`，单次观测 1.004 秒 |
| S3-13 | 真实 RPC Deadline | Linux 故障注入 | 通过 | `RPC_TIMEOUT`，单次观测 1.003 秒 |
| S3-14 | 慢 RPC 不阻塞其他连接 heartbeat | Linux 双连接 | 通过 | 3 秒慢登录期间 heartbeat 单次观测 0.001 秒 |
| S3-15 | 断线后迟到 RPC 不提交 Presence | Linux | 通过 | `stage3_rpc_lifecycle_probe: PASS`，Redis key 为 `(nil)` |
| S3-16 | UserService 快速重启 | Linux + ZooKeeper | 通过 | 等待旧临时节点后 3.878 秒内启动并恢复注册 |
| S3-17 | Stage 1 协议与会话安全回归 | Linux | 通过 | `server_frame_probe`、`session_security_probe` 均通过 |
| S3-18 | Stage 2 双节点集群回归 | Linux | 通过 | 本地/跨节点/Presence/离线/Nginx 全部通过，3/3 分流 |

表中的秒数是单次功能测试观测，用于证明有界响应和线程隔离，不是性能基准，不代表
平均值、P95、P99 或容量结论。

## 5. Linux 构建与基础服务

统一根工程实际完成配置和构建：

```bash
cmake --fresh -S . -B build/stage3 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/stage3 -j"$(nproc)" \
  --target ChatServer UserService \
           stage3_rpc_frame_tests stage3_rpc_protocol_tests \
           stage3_user_service_rpc_probe stage3_rpc_transport_probe
```

生成并检查的目标包括：

```text
chatserver/bin/ChatServer
services/user_service/bin/UserService
build/stage3/tests/stage3/stage3_user_service_rpc_probe
build/stage3/tests/stage3/stage3_rpc_transport_probe
```

MySQL、Redis、ZooKeeper 均实际启动；UserService 监听 `127.0.0.1:7001`。ZooKeeper
以下节点均返回同一内部端点：

```text
/nexusim.user.UserService/Login      -> 127.0.0.1:7001
/nexusim.user.UserService/Register   -> 127.0.0.1:7001
/nexusim.user.UserService/QueryUser  -> 127.0.0.1:7001
```

## 6. RPC 功能与传输

Linux CTest 最终结果：

```text
stage3_rpc_frame_tests       Passed
stage3_rpc_protocol_tests    Passed
100% tests passed, 0 tests failed out of 2
```

真实 UserService 探针创建测试账号后依次验证：

- Register 成功并返回用户 ID；
- 正确密码 Login 成功；
- 错误密码被 `USER_ERROR_INVALID_CREDENTIALS` 拒绝；
- QueryUser 返回匹配的公开用户信息；
- 数据库密码列为 PBKDF2 哈希，不是明文。

传输探针直连 `:7001` 验证了拆分帧、单次写入两个帧、畸形 Protobuf Envelope 和零长度
RPC 帧，最终输出：

```text
stage3_user_service_rpc_probe: PASS user_id=27
stage3_rpc_transport_probe: PASS
```

## 7. 故障、Deadline 与线程模型

### 7.1 服务发现和服务故障

分别停止 UserService、使 RPC 端点不可达及停止 ZooKeeper 后，ChatServer 没有崩溃，
客户端在配置上限内收到了明确错误：

```text
USER_SERVICE_UNAVAILABLE  response_seconds=0.005
RPC_UNAVAILABLE           response_seconds=0.004
ZOOKEEPER_UNAVAILABLE     response_seconds=1.004
```

### 7.2 真实 Deadline

UserService 对指定测试用户延迟 3 秒，同时将 ChatServer Deadline 临时设为 1 秒。探针
实际输出：

```text
stage3_rpc_failure_probe: PASS
(error=RPC_TIMEOUT, response_seconds=1.003)
```

这证明一次调用的总截止时间能够中止等待并向客户端返回结构化超时。Provider 不会强杀
已经进入 MySQL/业务函数的线程；超时后的迟到结果会被丢弃。

### 7.3 EventLoop 非阻塞

一个连接执行 3 秒慢 Login RPC 时，另一个已认证连接继续发送 heartbeat：

```text
stage3_eventloop_nonblocking_probe: PASS
(heartbeat_seconds=0.001, slow login completed successfully)
```

该结果证明本次场景下同步 RPC/数据库等待没有占用 Muduo EventLoop，也没有阻塞其他
连接的 heartbeat。该单次时间不作为延迟性能指标。

### 7.4 连接生命周期

客户端发送慢 Login 后立即断开，等待业务调用完成后检查 Redis：

```text
stage3_rpc_lifecycle_probe: PASS (no Presence after stale RPC)
redis-cli GET nexusim:presence:user:27 -> (nil)
```

迟到结果没有认证已关闭连接，也没有创建残留 Presence。

## 8. 快速重启问题与修复验证

Linux 首次快速重启暴露了 ZooKeeper 临时方法节点在旧进程退出后仍短暂存在的问题。原
5 秒边界会导致新 UserService 注册失败。修复包含：

- ZooKeeper 注册等待总上限配置 `MPRPC_ZK_REGISTRATION_TIMEOUT_MS=15000`；
- Login/Register/QueryUser 共用一个注册截止时间，不逐方法累加；
- 启动脚本按连接和注册期限等待端口就绪；
- 停止脚本确认旧进程退出，不能无条件报告成功；
- 关键等待和失败原因进入 `user-service.log`。

修复后连续执行“停止、启动、再次停止、立即启动”，日志实际进入
`waiting for stale ephemeral znode` 分支，第二次启动耗时 3.878 秒，随后：

```text
127.0.0.1:7001 LISTEN
/nexusim.user.UserService/Login -> 127.0.0.1:7001
```

对应修复提交：

- `03a90dd`：启用 ZooKeeper threaded C client API；
- `15d7009`：初版临时节点重启保护；
- `ba686ac`：可配置、共享截止时间及严格启停脚本。

## 9. 最终回归

关闭全部测试延迟并恢复正式测试配置：

```text
USER_SERVICE_TEST_DELAY_USER_ID=-1
USER_SERVICE_TEST_DELAY_MS=0
MPRPC_ZK_REGISTRATION_TIMEOUT_MS=15000
CHAT_RPC_DEADLINE_MS=2000
```

最终运行状态：

```text
ChatServer-1  127.0.0.1:6001
ChatServer-2  127.0.0.1:6002
UserService   127.0.0.1:7001
Nginx Stream  0.0.0.0:8000
Redis channel chatserver-1 subscribers = 1
Redis channel chatserver-2 subscribers = 1
```

最终回归实际输出：

```text
1/2 Test #1: stage3_rpc_frame_tests ........... Passed
2/2 Test #2: stage3_rpc_protocol_tests ........ Passed
100% tests passed, 0 tests failed out of 2
stage3_source_guards: PASS
server_frame_probe: PASS
session_security_probe: PASS (sender_id=43, receiver_id=42)
test 1 local direct chat: PASS
test 3 cross-node duplicate login: PASS
test 2 bidirectional Redis cross-node chat: PASS
test 4 authenticated Presence renewal: PASS
recipient existence validation: PASS
offline sender-side single storage: PASS
test 5 Nginx stream distribution: PASS {'chatserver-1': 3, 'chatserver-2': 3}
cluster_probe: PASS
Stage 3 final regression: PASS
```

## 10. 已知限制

1. V1 只有一个 UserService 实例，没有 RPC 多实例负载均衡和复杂服务治理。
2. MPRPC 每次调用使用短连接，没有连接池、自动重试或熔断。
3. ZooKeeper 会话在服务运行期间失效后的自动重新注册不在 Stage 3 范围。
4. Provider 不强制终止已经进入业务函数的超时任务，只阻止迟到结果继续提交。
5. 没有 TLS，系统只能运行在隔离测试环境，不能开放公网。
6. 未执行吞吐量、并发容量、P95/P99、长期稳定性、ASan 或 Valgrind 测试。
7. Stage 2 Redis Pub/Sub 仍不提供可靠 ACK、重试或持久化消费。
8. Qt5 客户端、AIService 和完整 Docker Compose 尚未实现或验收。

## 11. 可复现命令入口

- 构建与启动步骤：[`stage3-mprpc-user-service.md`](stage3-mprpc-user-service.md)
- C++ 测试目标：[`../tests/stage3/CMakeLists.txt`](../tests/stage3/CMakeLists.txt)
- UserService RPC 探针：[`../tests/stage3/user_service_rpc_probe.cpp`](../tests/stage3/user_service_rpc_probe.cpp)
- RPC 传输探针：[`../tests/stage3/rpc_transport_probe.cpp`](../tests/stage3/rpc_transport_probe.cpp)
- 故障探针：[`../tests/stage3/rpc_failure_probe.py`](../tests/stage3/rpc_failure_probe.py)
- EventLoop 探针：[`../tests/stage3/eventloop_nonblocking_probe.py`](../tests/stage3/eventloop_nonblocking_probe.py)
- 生命周期探针：[`../tests/stage3/rpc_lifecycle_probe.py`](../tests/stage3/rpc_lifecycle_probe.py)

所有“通过”结论均来自本地实际执行结果或用户提供的 Linux 终端原始输出。报告没有
把未执行项、设计意图或推测的吞吐量/延迟数据记为通过。
