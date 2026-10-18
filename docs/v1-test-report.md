# NexusIM V1 测试报告

> 报告日期：2026-10-18（Asia/Shanghai）  
> 被测提交：`718ff03979365822f16c9a139d94b85cc74481f6`  
> 当前结论：**阶段 1 已达到当前范围的验收条件；阶段 2 源码与部署准备完成，但 Linux 双节点集群尚未完成最终验收；完整 V1 暂不具备发布条件。**

## 1. 执行摘要

- TCP 4 字节大端分帧、粘包/半包、多帧、非法长度、连接级顺序、有界执行器、独立心跳执行器和 PendingRequest 生命周期测试已经通过。
- Linux 实机已验证 ChatServer 编译启动、注册登录、PBKDF2 新密码、服务端身份覆盖、未认证限制、心跳和 Redis Presence 的 TTL 续租及安全删除。
- Stage 2 的集群协议单测、线程边界源码守卫和 Python 探针语法检查已经通过；Nginx 1.28.3 与动态 Stream 模块已安装。
- 尚未取得双 ChatServer 跨节点 Pub/Sub、Nginx `:8000` 分流、Redis 停机、节点崩溃 TTL 的完整 Linux 通过日志，因此不能宣称集群验收通过。
- V1 规划中的 UserService/MPRPC 拆分、Qt 5.12.8 客户端、AIService、Docker Compose 尚未实现或验收。本报告不能作为完整 V1 发布批准。
- 未执行吞吐量、P95/P99 延迟、长期稳定性或内存泄漏测试，本报告不提供任何推测性性能数据。

## 2. 测试范围与判定规则

本报告覆盖当前仓库已经进入实现或可执行测试的功能：

1. Stage 1：TCP FrameCodec、业务线程隔离、connection strand、PendingRequest、认证会话、密码安全、MySQL schema、Redis Presence。
2. Stage 2：Redis Pub/Sub 路由协议、双 ChatServer 部署脚本、Nginx Stream 配置、集群与故障探针。
3. 已规划但尚未实现的 V1 模块仅记录缺口，不按“失败”伪装成已执行测试。

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
| Qt | `E:\Qt\Qt5.12.8` 存在；本报告未编译 Qt 客户端 |
| 限制 | 无 WSL/Muduo/MySQL/hiredis/Nginx 集成环境，不替代 Linux 实机验收 |

### 3.2 Linux 目标机（基于用户提供的真实输出）

| 项目 | 实际信息 |
|---|---|
| 仓库路径 | `/home/aoi/NexusIM/NexusIM` |
| 编译器 | GCC 15.2.0 |
| OpenSSL | 3.5.5 |
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
| S2-03 | Pub/Sub 空闲连接不因普通读超时退出 | Windows静态检查 | 通过 | 提交 `8838473`，源码守卫通过；Linux长时间空闲待验证 |
| S2-04 | Presence 失败响应先发送再断开 | Windows静态检查 | 通过 | 提交 `718ff03`，源码守卫通过；Redis停机实测待执行 |
| S2-05 | 同节点单聊 | Linux双节点 | 待执行 | `cluster_probe.py` 已具备测试逻辑 |
| S2-06 | 双向跨节点 Redis Pub/Sub 单聊 | Linux双节点 | 待执行 | 尚无 `cluster_probe: PASS` 输出 |
| S2-07 | 跨节点重复登录拒绝 | Linux双节点 | 待执行 | 探针预期 `errno=2` |
| S2-08 | 离线消息仅发送节点写入一次 | Linux双节点 | 待执行 | 探针验证唯一一条离线消息 |
| S2-09 | Nginx Stream 长连接分流 | Linux + Nginx | 待执行 | 模块已安装；尚无 `nginx -t`、`:8000` 和探针通过证据 |
| S2-10 | Redis 停机显式错误与离线回退 | Linux故障注入 | 待执行 | `redis_failure_probe.py` 尚未执行完成 |
| S2-11 | ChatServer 崩溃后的 Presence TTL 清理 | Linux故障注入 | 待执行 | `presence_crash_probe.py` 尚未执行完成 |
| V1-01 | UserService 经 MPRPC 的 Login/Register/QueryUser | Linux | 未实现 | `mprpc/` 仍是课程工程源码快照，尚未完成业务拆分 |
| V1-02 | Qt 5.12.8 登录、注册、单聊、AI窗口 | Windows | 未实现 | 当前阶段明确未开发 Qt UI |
| V1-03 | AIService + LLM API | Linux | 未实现 | 尚未开发，不存在真实 API 测试 |
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
| `/tmp/nexusim-nginx.conf` 未生成 | Nginx 无法读取配置 | 已明确动态模块加载和运行配置生成步骤；尚待实机成功输出 |

## 8. 已知限制与风险

1. **无 TLS。** 客户端密码、消息和 Redis 链路不具备传输加密，只能在隔离测试网络运行。
2. **Pub/Sub 不可靠。** 没有端到端 ACK、重试、去重或持久化消费；订阅者断开、队列满或 Presence 竞态时可能丢消息。
3. **单 Redis 故障域。** 当前阶段没有 Redis Sentinel/Cluster；Redis 不可用时登录和续租按 fail-closed 处理。
4. **最近修复缺少 Linux 集群回归。** `8838473` 和 `718ff03` 已通过本地源码守卫，但必须用真实空闲连接和 Redis 停机测试确认。
5. **安全验证仍不完整。** SQL 转义已实现，但缺少单引号、超长 UTF-8、畸形 JSON 批量输入等专项黑盒记录。
6. **没有性能结论。** 未执行并发连接、吞吐量、延迟分位、长稳、资源占用和 Valgrind/ASan，因此不能给出容量指标。
7. **完整 V1 功能缺口。** RPC UserService、Qt5 客户端、AIService 和 Docker Compose 未完成。

## 9. 发布判定

### Stage 1

**结论：有条件通过。** 已验证核心协议、安全会话、注册登录和 Presence；在进入公网或生产环境前仍需 TLS、专项 SQL 输入测试及性能/内存测试。

### Stage 2

**结论：暂不通过最终验收。** 源码、配置、探针和本地协议测试已准备好，但缺少 Linux 双节点和故障注入的实际通过输出。

### 完整 V1

**结论：不通过发布验收。** 原因不是现有测试失败，而是 UserService RPC、Qt5、AIService、Docker Compose 等承诺范围尚未实现。

## 10. 下一轮必跑验收

按顺序执行并保存完整输出：

```bash
# 两个节点和 Redis 订阅
ss -ltnp | grep -E ':6001|:6002'
redis-cli PUBSUB NUMSUB \
  nexusim:chat:node:chatserver-1 \
  nexusim:chat:node:chatserver-2

# Nginx 配置、入口与完整集群
sudo nginx -t -c "$NGINX_CONFIG"
ss -ltnp | grep ':8000'
python3 tests/stage2/cluster_probe.py

# 故障注入
python3 tests/stage2/redis_failure_probe.py
python3 tests/stage2/presence_crash_probe.py --port 6001 --user-id <id>

# SQL 特殊输入与旧密码最终确认
sudo mysql -D nexusim -e \
  'SELECT id,name,LEFT(password,20),state FROM `User` WHERE id=1;'
```

只有 `cluster_probe: PASS`、Redis 故障探针、节点崩溃 TTL 探针均通过后，才能把 Stage 2 改为通过。完整 V1 还需后续阶段分别提交对应测试记录。

## 11. 可追溯证据

- [阶段 1 实现与 Linux 记录](stage1-implementation.md)
- [阶段 2 架构、部署和测试步骤](stage2-cluster.md)
- [阶段 0 基线](phase0-baseline.md)
- [并发、异步 RPC 与 Presence 规则](concurrency-presence-design.md)
- [`tests/stage1/`](../tests/stage1/)
- [`tests/stage2/`](../tests/stage2/)
- 当前 Stage 2 主提交：`6e4f5b0`
- Redis 空闲订阅修复：`8838473`
- Presence 失败响应修复：`718ff03`

所有“通过”结论均来自本地实际执行结果或用户提供的 Linux 实际输出；没有把未执行项、预计结果或设计意图记为通过。
