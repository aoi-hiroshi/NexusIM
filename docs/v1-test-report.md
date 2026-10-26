# NexusIM V1 测试报告

> **2026-10-26 补充结论：**本报告原表格是 2026-10-20 的历史快照。
> Stage 5 真实 DeepSeek、Stage 6 Linux 双节点好友/群聊，以及 Stage 7
> Docker Compose 8 服务、持久化和故障注入现已通过。最新命令与结果见
> [Stage 6 报告](stage6-test-report.md)和[Stage 7 报告](stage7-test-report.md)。
> Stage 6 的双 Qt 图形客户端联调仍待单独验收；本文下方旧表中的“待执行”
> 不代表 2026-10-26 的当前状态。

> 报告更新日期：2026-10-20（Asia/Shanghai）
>
> Stage 2 Linux 被测提交：`718ff03979365822f16c9a139d94b85cc74481f6`
>
> Stage 3 Linux 被测提交：`ba686aca0e143cc9dc7b7b1faa28636bbddd8acb`
>
> 2026-10-20 当时结论（历史记录）：**阶段 1–3 已通过对应范围验收；Stage 4 的 Windows 双客户端联调已由用户确认通过。Stage 5 源码、Windows Qt 构建、核心/Mock 测试已完成，Linux 编译和真实 DeepSeek API 仍待测试机验收。完整 Docker Compose 仍未完成。**

## 1. 执行摘要

- TCP 4 字节大端分帧、粘包/半包、多帧、非法长度、连接级顺序、有界执行器、独立心跳执行器和 PendingRequest 生命周期测试已经通过。
- Linux 实机已验证 ChatServer 编译启动、注册登录、PBKDF2 新密码、服务端身份覆盖、未认证限制、心跳和 Redis Presence 的 TTL 续租及安全删除。
- Stage 2 的集群协议单测、线程边界源码守卫和 Python 探针语法检查已经通过；Linux 实机成功编译并启动两个 ChatServer，Redis 两个节点频道在空闲 10 秒后仍各保持 1 个订阅者。
- 本节点直发、双向跨节点 Pub/Sub、跨节点重复登录、离线单次入库、Nginx `:8000` 轮询分流、Redis 停机显式失败与离线回退、节点崩溃后 TTL 清理均取得真实通过输出。
- Stage 3 已完成 MPRPC 双向分帧、总 Deadline、Provider 线程隔离和独立 UserService；Windows 可移植测试与 Linux 真实 RPC、故障注入、生命周期和 Stage 1/2 回归均通过。
- Stage 4 Qt 5.12.8 客户端的 Windows 双端真实联调已由用户确认通过；早期报告中的待执行状态由此更新。
- Stage 5 已实现独立 AIService、DeepSeek HTTPS 适配、ChatServer AI RPC 和 Qt AI 页；Windows 本地 6/6 QtTest 和 1/1 AI 核心测试通过，Linux/真实 API 待验收。
- 未执行吞吐量、P95/P99 延迟、长期稳定性或内存泄漏测试，本报告不提供任何推测性性能数据。

## 2. 测试范围与判定规则

本报告覆盖当前仓库已经进入实现或可执行测试的功能：

1. Stage 1：TCP FrameCodec、业务线程隔离、connection strand、PendingRequest、认证会话、密码安全、MySQL schema、Redis Presence。
2. Stage 2：Redis Pub/Sub 路由协议、双 ChatServer 部署脚本、Nginx Stream 配置、集群与故障探针。
3. Stage 3：MPRPC 帧协议、Controller Deadline、Provider 有界执行器、独立 UserService、ChatService RPC 接入及目标机探针。
4. Stage 4：Qt5 FrameCodec、JSON 适配、RequestManager、Mock TCP、桌面 UI、Release 打包和双进程启动。
5. 已规划但尚未实现的 V1 模块仅记录缺口，不按“失败”伪装成已执行测试。

状态含义：

| 状态 | 含义 |
|---|---|
| 通过 | 有本轮或历史保留的真实命令输出支持 |
| 部分通过 | 部分断言通过，但仍缺关键运行证据 |
| 待执行 | 测试脚本或步骤存在，但没有真实通过输出 |
| 未实现 | 当前代码尚不具备该 V1 能力，无法验收 |

## 3. 被测环境

### 3.1 Windows 本地开发机

| 项目 | 实际信息 |
|---|---|
| 操作系统 | Windows，本地 Codex 工作区 |
| 编译器 | Visual Studio 2019，MSVC 19.29 |
| Windows SDK | 10.0.19041.0 |
| CMake | 3.20.21032501-MSVC_2，使用 VS2019 内置路径 |
| Python | 3.14.6 |
| Qt | 5.12.8 `msvc2017_64`；Debug/Release 客户端均已实际编译 |
| 限制 | 无 WSL/Muduo/MySQL/hiredis/Nginx 集成环境，不替代 Linux 实机验收 |

### 3.2 Linux 目标机（基于用户提供的真实输出）

| 项目 | 实际信息 |
|---|---|
| 仓库路径 | `/home/aoi/NexusIM/NexusIM` |
| 编译器 | GCC 15.2.0 |
| OpenSSL | 3.5.5 |
| Protobuf | 3.21.12 |
| ZooKeeper C client | 3.9.5 |
| 数据库 | MySQL，源库 `chat`，隔离目标库 `nexusim` |
| Redis | `127.0.0.1:6379`，实际 `PING` 返回 `PONG` |
| Nginx | 1.28.3-2ubuntu1.11 |
| Stream | `libnginx-mod-stream` 已安装，构建参数显示 `--with-stream=dynamic` |
| 部署限制 | 未启用 TLS，只允许隔离测试网络，不得开放公网 |

未提供 Redis Server 和 MySQL Server 的完整版本输出，因此本报告不推测其版本。

## 4. 测试结果总表

| 编号 | 测试项 | 平台 | 结果 | 证据/说明 |
|---|---|---|---|---|
| S1-01 | FrameCodec 每个拆分边界半包 | Windows + Linux | 通过 | `stage1_core_tests`、`server_frame_probe.py` |
| S1-02 | 逐字节半包、粘包、多帧、尾部半包 | Windows + Linux | 通过 | 服务端黑盒探针返回 `PASS` |
| S1-03 | 零长度与超过 1 MiB 帧拒绝 | Windows + Linux | 通过 | 四个短连接黑盒日志符合预期 |
| S1-04 | 同连接顺序、不同连接并行进展 | Windows | 通过 | `stage1_core_tests`；慢任务期间另一连接在 1 秒断言内完成 |
| S1-05 | 心跳不被业务 strand 长任务阻塞 | Windows | 通过 | 独立有界执行器单测通过 |
| S1-06 | PendingRequest 重复、超时、session 隔离、断线取消 | Windows + Linux | 通过 | Windows核心测试和 Linux Stage 1 CTest |
| S1-07 | PBKDF2 随机盐、正确/错误密码、畸形哈希关闭 | Windows + Linux | 通过 | `stage1_security_tests` |
| S1-08 | 256-bit session_id 长度与非复用 | Windows + Linux | 通过 | 安全单测及真实登录 64 字符返回 |
| S1-09 | MySQL 注册、登录和断线状态恢复 | Linux | 通过 | 两次注册登录成功；用户 2、3 断线后为 `offline` |
| S1-10 | 新密码不明文存储 | Linux | 通过 | 数据库实查前缀为 `pbkdf2_sha256$120000` |
| S1-11 | 旧账号登录兼容与密码升级 | Linux | 部分通过 | 用户 1 `zhangsan` 使用旧密码登录成功；尚缺升级后字段的最终 SQL 输出记录 |
| S1-12 | 客户端伪造 sender id/name/session_id | Linux | 通过 | 接收方看到服务端认证用户 5，且无 `session_id` 泄漏 |
| S1-13 | 未认证心跳和禁用旧 handler | Linux | 通过 | `session_security_probe.py` 返回 `PASS` |
| S1-14 | Presence TTL 续租 | Linux | 通过 | TTL `90 → 88 → 90` |
| S1-15 | 旧 session 安全删除 | Linux | 通过 | stale compare-delete 返回 0，当前 key 保持；断线后 key 删除 |
| S1-16 | SQL 特殊字符/注入专项黑盒测试 | Linux | 待执行 | 代码使用连接字符集转义，但没有收到专项恶意输入测试输出 |
| S2-01 | Presence/Delivery 编解码与非法值拒绝 | Windows | 通过 | `stage2_cluster_protocol_tests` 1/1 |
| S2-02 | Redis 订阅线程与 Muduo 状态隔离 | Windows静态检查 | 通过 | `stage2_source_guards.py` |
| S2-03 | Pub/Sub 空闲连接不因普通读超时退出 | Windows + Linux | 通过 | `8838473`；Linux 等待 10 秒后两个频道订阅数仍为 1，随后登录和集群探针通过 |
| S2-04 | Presence 失败响应先发送再断开 | Windows + Linux | 通过 | `718ff03`；Redis 停机探针收到显式 `PRESENCE_UNAVAILABLE` 并完整结束 |
| S2-05 | 同节点单聊 | Linux双节点 | 通过 | 直连探针与完整探针均输出 `test 1 local direct chat: PASS` |
| S2-06 | 双向跨节点 Redis Pub/Sub 单聊 | Linux双节点 | 通过 | A→B、B→A 均通过，`cluster_probe: PASS` |
| S2-07 | 跨节点重复登录拒绝 | Linux双节点 | 通过 | 探针输出 `test 3 cross-node duplicate login: PASS` |
| S2-08 | 离线消息仅发送节点写入一次 | Linux双节点 | 通过 | 探针输出 `offline sender-side single storage: PASS` |
| S2-09 | Nginx Stream 长连接分流 | Linux + Nginx | 通过 | 配置检查成功，`:8000` 监听；6 个连接按 `chatserver-1:3 / chatserver-2:3` 分配 |
| S2-10 | Redis 停机显式错误与离线回退 | Linux故障注入 | 通过 | `PRESENCE_UNAVAILABLE`、`queued_offline=true`，单次响应 0.428 秒 |
| S2-11 | ChatServer 崩溃后的 Presence TTL 清理 | Linux故障注入 | 通过 | `SIGKILL` 所属节点后 key 按初始 TTL 90 秒过期 |
| S3-01 | RPC 4 字节大端帧：半包/粘包/多帧/非法长度/>1024 | Windows + Linux | 通过 | `stage3_rpc_frame_tests`、`stage3_rpc_transport_probe` |
| S3-02 | MPRPC/UserService 线程与安全边界源码守卫 | Windows + Linux | 通过 | `stage3_source_guards.py` |
| S3-03 | UserService Login/Register/QueryUser 真实 RPC | Linux | 通过 | `stage3_user_service_rpc_probe: PASS user_id=27`；错误密码也被拒绝 |
| S3-04 | UserService/ZooKeeper 不可用和总超时 | Linux故障注入 | 通过 | `USER_SERVICE_UNAVAILABLE`、`RPC_UNAVAILABLE`、`ZOOKEEPER_UNAVAILABLE`、`RPC_TIMEOUT` 均有真实输出 |
| S3-05 | 慢 RPC 期间 heartbeat 不阻塞 | Linux双连接 | 通过 | 3 秒慢 RPC 期间 heartbeat 单次观测 0.001 秒 |
| S3-06 | RPC 中途断线不发送旧结果且不留 Presence | Linux | 通过 | `stage3_rpc_lifecycle_probe: PASS`，最终 Redis key 为 `(nil)` |
| S3-07 | UserService 快速重启与旧临时节点 | Linux + ZooKeeper | 通过 | 有界等待分支实际触发，3.878 秒恢复监听和注册 |
| S4-01 | Qt FrameCodec 半包/粘包/多帧/非法长度 | Windows QtTest | 通过 | Debug 与 Release `qt_framecodec_tests` 均通过 |
| S4-02 | JSON 协议与 RequestManager | Windows QtTest | 通过 | 协议及请求生命周期两组测试均通过 |
| S4-03 | QTcpSocket 异步收包、断线和连接拒绝 | Windows Mock TCP | 通过 | 本机 `QTcpServer` 驱动的 `qt_networkclient_tests` 通过 |
| S4-04 | Release 打包、启动和双开 | Windows | 通过 | Qt/VC142 运行库齐全；两个独立进程同时创建窗口 |
| S4-05 | Windows 客户端连接 Linux 双节点/Nginx | Windows + Linux | 通过（用户确认） | 用户报告双端测试均通过；本仓库未保存原始终端日志 |
| V1-01 | UserService 经 MPRPC 的 Login/Register/QueryUser | Linux | 通过 | Linux 构建、ZooKeeper 注册、真实 MySQL RPC 及密码哈希均通过 |
| V1-02 | Qt 5.12.8 登录、注册、单聊、AI 页 | Windows | 部分通过 | 双端联调由用户确认；Stage 5 AI 页 Windows 构建、单测和打包通过，真实 AI 待 Linux |
| V1-03 | AIService + LLM API | Linux | 部分通过 | C++ 实现与 Windows 可移植测试已完成；Linux 构建、Mock libcurl 及真实 API 待执行 |
| V1-04 | Docker Compose 全栈启动 | Linux | 未实现/待执行 | 当前只有 Stage 2 部署配置和脚本，未完成全栈 Compose 验收 |

## 5. 本轮重新执行结果

在提交 `718ff03` 上于 2026-10-18 重新执行：

```powershell
cmake -S tests/stage1 -B build/stage1-tests -G "Visual Studio 16 2019" -A x64
cmake --build build/stage1-tests --config Debug --parallel 4
ctest --test-dir build/stage1-tests -C Debug --output-on-failure

cmake -S tests/stage2 -B build/stage2-tests -G "Visual Studio 16 2019" -A x64
cmake --build build/stage2-tests --config Debug --parallel 4
ctest --test-dir build/stage2-tests -C Debug --output-on-failure

python tests/stage2/source_guards.py
python -m compileall -q tests/stage1 tests/stage2
```

实际结果：

| 命令 | 结果 |
|---|---|
| Stage 1 CTest | `stage1_core_tests` 1/1，通过，0.25 秒 |
| Stage 2 CTest | `stage2_cluster_protocol_tests` 1/1，通过，0.24 秒 |
| Stage 2 source guards | `stage2_source_guards: PASS` |
| Python compileall | 退出码 0，无语法错误输出 |

上述耗时只是单次测试运行时间，不是性能指标。

## 6. Linux 已取得的关键证据

### 6.1 TCP 分帧

```text
server_frame_probe: PASS
```

服务端日志记录四组独立连接，分别对应逐字节半包、粘包双帧、零长度帧和超限帧，随后均正常移除。

### 6.2 注册登录与密码

```text
REGISTER: {"errno": 0, "id": 2, "msgid": 5}
LOGIN: {"errno": 0, "id": 2, "msgid": 2, ...}
auth_probe: PASS
```

第二次执行创建用户 3 并再次通过。数据库实查两个账号：

```text
password_prefix = pbkdf2_sha256$120000
state = offline
```

### 6.3 身份校验

```text
HEARTBEAT: {"msgid": 12, "ok": true, ...}
DELIVERED: {"id": 5, "msgid": 6,
            "name": "security_sender_...", "toid": 4}
session_security_probe: PASS
```

发送请求中伪造的 `id/name/session_id` 未进入最终投递消息。

### 6.4 Presence

```text
PRESENCE: initial_ttl=90, before_renew=88,
          after_renew=90, stale_delete=0
presence_probe: PASS (user_id=1)
```

该结果支持“认证心跳刷新 TTL、旧 owner 不能删除当前 owner、当前连接断开后 key 删除”的结论。

### 6.5 Stage 2 Linux 构建与双节点启动

提交 `8838473` 和 `718ff03` 均在目标机重新增量构建，最终 `ChatServer` 链接成功。
两个实例实际监听：

```text
127.0.0.1:6001  ChatServer-1
127.0.0.1:6002  ChatServer-2
```

等待 10 秒后执行 `PUBSUB NUMSUB`：

```text
nexusim:chat:node:chatserver-1  1
nexusim:chat:node:chatserver-2  1
```

随后执行直连测试，实际输出：

```text
test 1 local direct chat: PASS
test 3 cross-node duplicate login: PASS
test 2 bidirectional Redis cross-node chat: PASS
test 4 authenticated Presence renewal: PASS
recipient existence validation: PASS
offline sender-side single storage: PASS
exit_code=0
```

### 6.6 Nginx Stream

目标机安装 Nginx `1.28.3-2ubuntu1.11` 和 `libnginx-mod-stream`，实际构建参数为
`--with-stream=dynamic`。使用包内真实模块路径
`/usr/lib/nginx/modules/ngx_stream_module.so` 生成独立运行配置后：

```text
nginx: configuration file /tmp/nexusim-nginx.conf test is successful
LISTEN 0 511 0.0.0.0:8000
```

完整探针实际输出：

```text
test 1 local direct chat: PASS
test 3 cross-node duplicate login: PASS
test 2 bidirectional Redis cross-node chat: PASS
test 4 authenticated Presence renewal: PASS
recipient existence validation: PASS
offline sender-side single storage: PASS
test 5 Nginx stream distribution: PASS {'chatserver-1': 3, 'chatserver-2': 3}
cluster_probe: PASS
exit_code=0
```

### 6.7 Redis 故障注入

操作者停止真实 Redis 服务并确认 `127.0.0.1:6379 Connection refused`。修复最终响应
与关闭竞态后，探针实际输出：

```text
redis_failure_probe: PASS
(explicit=PRESENCE_UNAVAILABLE, queued_offline=true, response_seconds=0.428)
```

`0.428` 秒只是本次单次观测，不是吞吐量或延迟指标。测试后 Redis 与两个
ChatServer 均按预案重启；当前 V1 不实现 Redis 自动重连。

### 6.8 节点崩溃 TTL

用户 12 直连 `chatserver-1` 登录后，对所属 ChatServer 执行 `SIGKILL`，Redis 保持
运行。实际结果：

```text
presence_crash_probe: PASS
(key expired after owner crash, initial_ttl=90)
```

这证明节点异常退出时没有依赖全局状态重置，旧 Presence 最终由 TTL 清理。

## 7. 测试中发现并处理的问题

| 问题 | 影响 | 处理状态 |
|---|---|---|
| Linux 缺少 hiredis 开发头文件 | ChatServer 无法编译 | 安装依赖后解决 |
| `main.cpp` 依赖隐式 Muduo namespace | Linux 编译失败 | `aedcd2f` 修复 |
| 数据库真实列为 `user_id/friend_id/group_id` | 登录读取关联数据和迁移 SQL 失败 | `42d9db6` 修复 |
| 原 OfflineMessage 以 user_id 为主键 | 每用户只能保存一条离线消息 | 新 `nexusim` 库升级为自增 id 主键 |
| `Aoi_@localhost` 无 `nexusim` 权限 | 真实注册失败，MySQL 1044 | 授予最小 CRUD 权限后通过 |
| 临时测试把 msgid 3 当成注册 | 收到 `UNAUTHENTICATED` | 确认 3 是注销、4 才是注册；固化 `auth_probe.py` |
| Redis Pub/Sub 订阅 socket 使用普通读超时 | 频道空闲后订阅线程退出 | `8838473` 修复并加入源码守卫 |
| Presence 失败时工作线程先 shutdown | 最终 `PRESENCE_LOST` 可能丢失 | `718ff03` 改为 EventLoop 内先发送再关闭 |
| 文档占位路径 `/path/to/NexusIM` 被直接执行 | 构建和 deploy 相对路径全部失败 | 已确认真实根目录 `/home/aoi/NexusIM/NexusIM` |
| `/tmp/nexusim-nginx.conf` 未生成或未加载动态模块 | Nginx 无法读取配置或报 `unknown directive "stream"` | 从已安装包取得模块路径后生成配置，`nginx -t`、`:8000` 和 3/3 分流实测通过 |

## 8. 已知限制与风险

1. **无 TLS。** 客户端密码、消息和 Redis 链路不具备传输加密，只能在隔离测试网络运行。
2. **Pub/Sub 不可靠。** 没有端到端 ACK、重试、去重或持久化消费；订阅者断开、队列满或 Presence 竞态时可能丢消息。
3. **单 Redis 故障域。** 当前阶段没有 Redis Sentinel/Cluster；Redis 不可用时登录和续租按 fail-closed 处理。
4. **Redis 不自动重连。** Redis 中断后当前进程按 fail-closed 处理，恢复 Redis 后仍需重启两个 ChatServer；自动退避重连不在 Stage 2 范围。
5. **安全验证仍不完整。** SQL 转义已实现，但缺少单引号、超长 UTF-8、畸形 JSON 批量输入等专项黑盒记录。
6. **没有性能结论。** 未执行并发连接、吞吐量、延迟分位、长稳、资源占用和 Valgrind/ASan，因此不能给出容量指标。
7. **完整 V1 验收缺口。** Qt5 和 AIService 代码已完成，但 Stage 5 尚缺 Linux/真实 API 证据，完整 Docker Compose 仍未完成。

## 9. 发布判定

### Stage 1

**结论：有条件通过。** 已验证核心协议、安全会话、注册登录和 Presence；在进入公网或生产环境前仍需 TLS、专项 SQL 输入测试及性能/内存测试。

### Stage 2

**结论：通过当前范围验收。** Linux 双节点本地与跨节点路由、重复登录、离线单次入库、Nginx 分流、Presence 生命周期、Redis 停机及节点崩溃 TTL 均有真实通过输出。该结论不扩展为端到端可靠投递、自动重连、TLS 或生产发布批准。

### Stage 3

**结论：通过当前范围验收。** Windows 可移植测试及 Linux 统一构建、ZooKeeper 注册、真实 UserService RPC、传输分帧、故障注入、Deadline、慢 RPC 隔离、断线生命周期、快速重启和 Stage 1/2 最终回归均通过。完整证据见 `docs/stage3-test-report.md`。

### 完整 V1

**结论：尚不通过完整发布验收。** 原因是 Stage 5 Linux/真实 API 和完整 Docker Compose 尚无运行证据，而不是当前 Windows 测试失败。

## 10. Stage 3 Linux 验收与保留回归

Stage 3 Linux 验收已经完成。后续阶段修改 ChatServer、MPRPC、UserService、Redis 或
连接生命周期代码时，必须继续保留以下回归门槛：

```bash
ctest --test-dir build/stage1-tests --output-on-failure
ctest --test-dir build/stage2-tests --output-on-failure
python3 tests/stage1/server_frame_probe.py --port 6001
python3 tests/stage1/session_security_probe.py --port 6001
python3 tests/stage2/cluster_probe.py
```

仍建议单独补充但不冒充已执行的安全证据：包含单引号、超长 UTF-8、畸形 JSON 的
专项输入测试，以及旧账号密码升级后的最终 SQL 输出。Stage 3 的真实 RPC 超时、
UserService 不可用、ZooKeeper 不可用、EventLoop 不阻塞和断线生命周期均已执行通过。

## 11. 可追溯证据

- [阶段 1 实现与 Linux 记录](stage1-implementation.md)
- [阶段 2 架构、部署和测试步骤](stage2-cluster.md)
- [阶段 3 MPRPC 与 UserService](stage3-mprpc-user-service.md)
- [阶段 3 Linux 实机测试报告](stage3-test-report.md)
- [阶段 0 基线](phase0-baseline.md)
- [并发、异步 RPC 与 Presence 规则](concurrency-presence-design.md)
- [`tests/stage1/`](../tests/stage1/)
- [`tests/stage2/`](../tests/stage2/)
- 当前 Stage 2 主提交：`6e4f5b0`
- Redis 空闲订阅修复：`8838473`
- Presence 失败响应修复：`718ff03`
- Stage 3 MPRPC/UserService 主实现：`a32b8d7`
- ZooKeeper threaded API 修复：`03a90dd`
- UserService 快速重启最终修复：`ba686ac`

所有“通过”结论均来自本地实际执行结果或用户提供的 Linux 实际输出；没有把未执行项、预计结果或设计意图记为通过。
