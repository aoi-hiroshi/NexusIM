# NexusIM 阶段 3：MPRPC + UserService

## 1. 当前结论

阶段 3 已完成 Windows 工作区源码实现、平台无关测试及 Linux 目标机实机验收。
完整证据见 [`stage3-test-report.md`](stage3-test-report.md)。以下结果已真实执行：

- VS2019 `stage3_rpc_frame_tests`：1/1 通过；
- VS2019 阶段 1 `stage1_core_tests`：1/1 回归通过；
- VS2019 阶段 2 `stage2_cluster_protocol_tests`：1/1 回归通过；
- `tests/stage2/source_guards.py`：通过；
- `tests/stage3/source_guards.py`：通过；
- Stage 1/2/3 Python 探针 `compileall`：通过。
- Linux 根 CMake：ChatServer、UserService 和四个 Stage 3 测试目标构建成功；
- Linux CTest：RPC frame/protocol 2/2 通过；
- Linux 真实 UserService Register/Login/QueryUser 与 RPC transport 探针：通过；
- UserService/ZooKeeper 故障、RPC Deadline、EventLoop 非阻塞和断线生命周期：通过；
- Stage 1/2 Linux 最终回归：通过。

Windows 上也真实执行了统一根 CMake 配置，停止于：

```text
Could NOT find Protobuf (missing: Protobuf_INCLUDE_DIR)
```

这与当前机器没有 `protoc`/Protobuf C++ 开发包的环境检查一致，不记为 Windows 服务端
构建通过。Linux 构建结果由目标机独立验证并记录在测试报告中。

本轮还尝试重新启用 `stage1_security_tests`，CMake 找到 OpenSSL 3.6.3 头文件但未找到
`OPENSSL_CRYPTO_LIBRARY`，因此该测试没有在本轮重新执行。历史 Linux PBKDF2 证据
仍保留在 V1 报告中；UserService 的真实哈希行为已在 Linux Stage 3 验收中重测通过。

Windows 没有安装 Linux Muduo、ZooKeeper C client 和 Protobuf 工具链，因此 Windows
结果未替代 Linux 证据。本文第 8 节以后保留可复现的目标机构建与验收命令。

## 2. 进程与调用关系

```text
Client framed JSON
  -> Muduo ChatServer EventLoop（解帧、轻量校验、投递）
  -> connection strand / bounded business worker
  -> UserServiceClient + generated UserService_Stub
  -> MprpcChannel（ZooKeeper 发现 + 总 Deadline + framed socket）
  -> RpcProvider EventLoop（累积解帧、投递）
  -> RpcProvider bounded executor
  -> UserServiceImpl
  -> UserModel（每次调用独立 MySQL 连接）
```

ChatServer 保留 Redis Presence、Redis Pub/Sub、TcpConnection、离线消息和好友数据的
管理。UserService 不接触 Redis 或客户端连接，只负责：

- `Login`：查询 `User`、PBKDF2 验证、旧明文密码成功登录后的哈希迁移；
- `Register`：PBKDF2-HMAC-SHA256 哈希后写入原 `User` 表；
- `QueryUser`：仅返回 `user_id + username`，不返回密码、哈希或盐。

ChatService 不再直接调用 UserModel 完成登录、注册或接收者资料查询，也不再使用
MySQL `state` 作为实时在线状态。Redis Presence 仍是集群在线状态的唯一依据。

## 3. RPC 线协议

请求和响应使用同一协议：

```text
4-byte unsigned big-endian frame_length
+ serialized mprpc.RpcEnvelope
```

Envelope 包含：

- `protocol_version`；
- request/response 类型；
- `request_id`；
- `service_name` 和 `method_name`；
- Protobuf `payload`；
- `status_code` 和 `error_message`；
- `deadline_unix_ms`。

默认最大 RPC Envelope 为 4 MiB，可用 `MPRPC_MAX_FRAME_BYTES` 调整。Provider 基于
Muduo Buffer 循环解析完整帧，保留半包尾部并一次处理多个完整帧。Channel 使用
`receiveExact` 读取响应头和任意长度响应，不再使用课程代码的单次 `recv(1024)`。

非法长度、Envelope 解析失败、未知服务/方法、队列满、Deadline 超时和业务异常均返回
结构化状态。无 request_id 的非法帧返回错误后关闭连接。

## 4. Deadline 与资源生命周期

`MprpcController` 增加错误码、取消状态和 steady-clock Deadline。一个总 Deadline 覆盖：

1. ZooKeeper 建连；
2. ZooKeeper 方法节点查询；
3. 非阻塞 socket connect；
4. 完整 send；
5. 帧头和完整响应 recv。

Channel 使用 RAII socket，所有错误路径都会关闭描述符，不会调用 `exit()` 终止
ChatServer。ZooKeeper 建连和异步查询均有等待上限。V1 每次调用建立短连接，不实现
连接池、重试、熔断或多实例负载均衡。

## 5. Provider 线程模型

RpcProvider I/O 线程只执行累积解帧和向有界队列投递。Envelope 解析、Service 查找、
请求反序列化以及 UserService MySQL 操作均在 Provider 有界工作线程中执行。完成回调
持有本次 request/response 的受控生命周期，最终帧通过 TcpConnection 所属 EventLoop
发送。队列满返回 `RPC_STATUS_SERVER_BUSY`。

默认配置：

```text
MPRPC_PROVIDER_IO_THREADS=2
MPRPC_PROVIDER_THREADS=4
MPRPC_PROVIDER_QUEUE=512
```

## 6. ChatServer 生命周期与 Presence 提交

每条连接建立时生成随机 `lifecycle_id`，用于未认证 Login/Register 的 PendingRequest。
认证成功后仍生成独立 256-bit `session_id`。登录顺序如下：

1. connection strand 注册 PendingRequest；
2. 工作线程同步调用 `UserService.Login`；
3. 检查 request 未取消且 lifecycle 仍属于当前连接；
4. Redis `SET NX EX` 占用 Presence；
5. 回投原 EventLoop，重新检查连接并绑定本地 Session；
6. 绑定失败时用 node_id + connection_id + session_id 比较后删除 Presence；
7. Session 提交完成后发送登录响应。

断线会撤销 lifecycle 对应的全部 RPC PendingRequest。迟到 RPC 不会发送到重连连接，
也不会创建 Presence。Heartbeat 继续使用独立 Presence executor，不排在慢 RPC 所在的
connection strand 后面。

## 7. 环境变量

ChatServer 新增：

```text
CHAT_RPC_DEADLINE_MS=2000
MPRPC_ZOOKEEPER_IP=127.0.0.1
MPRPC_ZOOKEEPER_PORT=2181
MPRPC_ZK_CONNECT_TIMEOUT_MS=1000
MPRPC_ZK_SESSION_TIMEOUT_MS=3000
MPRPC_MAX_FRAME_BYTES=4194304
```

UserService 使用 `deploy/config/user-service.env.example`。数据库仍使用原 `nexusim`
Schema 和大小写敏感表 `User`，没有创建第二套用户表。测试延迟变量默认禁用：

```text
MPRPC_ZK_REGISTRATION_TIMEOUT_MS=15000
USER_SERVICE_TEST_DELAY_USER_ID=-1
USER_SERVICE_TEST_DELAY_MS=0
```

`MPRPC_ZK_REGISTRATION_TIMEOUT_MS` 是 UserService 快速重启时等待旧临时方法节点
过期的总上限；三个方法共享同一个截止时间，不会逐方法叠加。测试延迟变量只用于慢
RPC 验收，不应在正常运行中启用。停止脚本会确认旧进程确实退出；若 SIGTERM 后仍存活，
脚本保留 PID 文件并返回失败，不会错误宣称已停止。

## 8. Ubuntu 依赖检查与构建

先检查目标机实际版本，不在文档中假设版本号：

```bash
cd "$HOME/NexusIM/NexusIM"

cmake --version
g++ --version
protoc --version
pkg-config --modversion protobuf || true
pkg-config --modversion openssl || true
mysql_config --version

test -f /usr/include/muduo/net/TcpServer.h && echo 'muduo headers: OK'
test -f /usr/include/zookeeper/zookeeper.h && echo 'zookeeper headers: OK'
ldconfig -p | grep -E 'muduo_(net|base)|protobuf|zookeeper_mt|mysqlclient|hiredis'
```

从仓库根目录统一构建：

```bash
cmake -S . -B build/stage3 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/stage3 -j"$(nproc)" \
  --target ChatServer UserService \
           stage3_rpc_frame_tests stage3_rpc_protocol_tests \
           stage3_user_service_rpc_probe stage3_rpc_transport_probe

ls -l chatserver/bin/ChatServer
ls -l services/user_service/bin/UserService
```

如配置失败，应记录 CMake 的真实缺失依赖，不得通过硬编码 `/usr/lib64` 绕过发现。

## 9. 启动顺序

1. MySQL 及原 `nexusim` Schema；
2. Redis；
3. ZooKeeper；
4. UserService；
5. 两个 ChatServer；
6. Nginx Stream。

配置和启动：

```bash
cp deploy/config/user-service.env.example deploy/config/user-service.env
# 编辑数据库账号，不要提交实际配置

# 已有 chatserver-1.env/chatserver-2.env 可保留，再补入第 7 节 RPC 变量；
# 或重新从 .env.example 复制后填写原数据库配置。
chmod 600 deploy/config/user-service.env \
  deploy/config/chatserver-1.env deploy/config/chatserver-2.env
chmod +x deploy/scripts/*.sh

deploy/scripts/start-user-service.sh
sleep 2
deploy/scripts/start-chatservers.sh
```

检查注册和端口：

```bash
ss -ltnp | grep -E ':7001|:6001|:6002'
zkCli.sh get /nexusim.user.UserService/Login
zkCli.sh get /nexusim.user.UserService/Register
zkCli.sh get /nexusim.user.UserService/QueryUser
tail -n 100 run/user-service/logs/user-service.log
tail -n 100 run/chatservers/logs/chatserver-1.log
```

## 10. Linux 验收命令（已执行，保留用于复现）

纯协议及真实 RPC：

```bash
ctest --test-dir build/stage3 --output-on-failure
python3 tests/stage3/source_guards.py

export NEXUSIM_RPC_PROBE_PASSWORD='仅用于测试的新密码'
build/stage3/tests/stage3/stage3_user_service_rpc_probe

# 7001 为 UserService 直连端口，最后一个参数为已存在 user_id
build/stage3/tests/stage3/stage3_rpc_transport_probe 127.0.0.1 7001 <user_id>
```

服务不可用和 ZooKeeper 不可用分别停掉目标服务后执行：

```bash
python3 tests/stage3/rpc_failure_probe.py \
  --port 6001 --user-id <id> --password '<password>'
```

慢 RPC 非阻塞测试：先在 UserService 配置中仅对 `<slow_id>` 设置
`USER_SERVICE_TEST_DELAY_MS=3000`，并令 ChatServer 的
`CHAT_RPC_DEADLINE_MS=5000`，然后执行：

```bash
python3 tests/stage3/eventloop_nonblocking_probe.py \
  --port 6001 \
  --slow-user-id <slow_id> --slow-password '<password>' \
  --healthy-user-id <healthy_id> --healthy-password '<password>'
```

断线生命周期测试使用同一个延迟用户：

```bash
python3 tests/stage3/rpc_lifecycle_probe.py \
  --port 6001 --user-id <slow_id> --password '<password>' \
  --wait-seconds 4
```

最后关闭延迟配置、重启 UserService 和 ChatServer，再执行阶段 1/2 回归：

```bash
python3 tests/stage1/server_frame_probe.py --port 6001
python3 tests/stage1/session_security_probe.py --port 6001
python3 tests/stage2/cluster_probe.py
```

## 11. 当前明确限制

- V1 单 UserService 实例，每次 RPC 使用一个短连接；
- Provider 不强杀已经进入 MySQL 调用的任务，Deadline 后只丢弃迟到响应；
- ZooKeeper 会话失效后的自动重新注册不在本阶段范围；
- QueryUser 仅返回公开字段，UserService/RPC 端口必须绑定内部地址；
- 无 TLS，只允许隔离测试环境；
- Stage 2 的 Redis Pub/Sub 仍不提供 ACK、重试或可靠持久化。
