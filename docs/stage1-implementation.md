# 阶段 1：协议、安全与并发基线

## 已实现范围

- 客户端与 ChatServer 统一采用 `4-byte big-endian length + JSON payload`；空帧和超过 1 MiB 的帧会被拒绝。
- Muduo `Buffer` 只在 I/O 线程增量解帧。普通业务投递到固定线程数、有界队列的 connection strand；同一连接串行、不同连接可并行。
- 心跳绕过普通业务 strand，但不会绕过认证。它只读取服务端会话快照，然后在独立有界 Presence 执行器中执行 Redis 比较续租。
- 每个 TCP 连接有服务端维护的 `connection_id`；登录后获得 256-bit 随机 `session_id`。单聊的 `id/name` 一律由会话覆盖。
- Redis Presence 值为 `node_id|connection_id|session_id`，登录使用 `SET NX EX`，续租/删除使用 compare-and-expire / compare-and-delete Lua。
- PendingRequest 按 request/connection/session/deadline 管理；断线取消，周期清理超时，迟到结果不会发送到新会话。回投连接使用弱引用。
- V1 网络 allowlist 仅注册 Login、Register、Logout、DirectChat。好友和群组旧 handler 未注册，访问时返回 `FEATURE_DISABLED`。
- 数据库连接参数改为环境变量；没有 `CHAT_DB_PASSWORD` 时拒绝连接。外部字符串在当前连接字符集下转义。
- 新密码使用随机盐 PBKDF2-HMAC-SHA256（120,000 次）。旧明文记录只允许在一次成功登录时迁移，迁移写入失败则登录失败。

## 连接 strand 规则

完整帧按接收顺序进入每连接队列。一个连接同时最多执行一个普通业务任务，任务结束后再取下一项；连接断开时清除尚未开始的任务。全局 runnable lane 队列和每连接队列都有上限，满时立即返回 `SERVER_BUSY`，I/O 线程不等待。

心跳是唯一的旁路：它不能改变业务顺序或携带用户身份，只能为当前已认证 session 续租。续租失败会让本地 session 失效并关闭连接。Redis 调用仍在 Presence 工作线程，不在 EventLoop。

## V1 已知投递限制

Redis Pub/Sub 不是可靠队列。Presence 检查与 publish 之间存在竞态，订阅连接中断时也可能丢消息；V1 没有端到端 ACK、重试和去重，因此不能承诺完整可靠投递。离线判断明确时会写 MySQL，publish 命令失败也回退写离线表，但这不能消除所有窗口。

V1 没有 TLS，只允许部署在隔离测试网络，禁止把 Nginx、ChatServer、MySQL、Redis 或后续 RPC 端口暴露到公网。TCP 中的密码和消息没有传输加密保护。

## 本机实际测试记录（2026-10-17）

环境：Windows、Visual Studio 2019 MSVC 19.29、CMake 3.20（VS2019 内置）。执行：

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' `
  -S tests/stage1 -B build/stage1-tests -G 'Visual Studio 16 2019' -A x64
& 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' `
  --build build/stage1-tests --config Debug --parallel 4
& 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' `
  --test-dir build/stage1-tests -C Debug --output-on-failure
```

结果：`stage1_core_tests` 1/1 通过，并连续重复执行 20/20 通过。覆盖每个拆分边界、逐字节半包、粘包、多帧、尾部半包、零长度、超限帧、同连接顺序、慢连接期间另一连接可响应、心跳执行器独立，以及 PendingRequest 重复、超时、session 不匹配和断线取消。

另使用本机 MSYS2 UCRT64 GCC 与 OpenSSL 编译运行 `stage1_security_tests`，结果通过；覆盖随机盐导致相同密码产生不同哈希、正确/错误密码验证、畸形哈希安全失败、256-bit session ID 长度与非复用。该测试在 Linux 可通过 `NEXUSIM_BUILD_SECURITY_TESTS=ON` 纳入 CTest。

本机没有 WSL/Muduo/MySQL/hiredis Linux 开发环境，因此没有声称 ChatServer Linux 链接或 Redis/MySQL 集成已经通过。下面的测试必须在目标 Linux 重跑并记录输出。

## Linux 构建与验证

```bash
cmake -S tests/stage1 -B build/stage1-tests -DCMAKE_BUILD_TYPE=Debug -DNEXUSIM_BUILD_SECURITY_TESTS=ON
cmake --build build/stage1-tests -j
ctest --test-dir build/stage1-tests --output-on-failure

cmake -S chatserver -B build/chatserver -DCMAKE_BUILD_TYPE=Debug
cmake --build build/chatserver -j
mysql -u root -p chat < chatserver/sql/001_stage1_security.sql
```

按 `chatserver/.env.example` 设置环境变量，但不要提交实际密码。先启动 MySQL/Redis，再启动 ChatServer，执行：

```bash
python3 tests/stage1/server_frame_probe.py --host 127.0.0.1 --port 6000
```

集成验收还必须人工或自动记录：

1. 注册后确认 `user.password` 是 `pbkdf2_sha256$...`，不是原密码；包含单引号的用户名和消息不改变 SQL 结构。
2. 两个连接登录 A/B，A 发送伪造的 `id/name`，B 收到的发送者仍是 A 的认证身份。
3. 用代理延迟 MySQL/Redis 请求时，另一个连接仍能收到协议错误/业务响应；EventLoop 线程栈中不出现同步数据库、Redis 或 RPC 调用。
4. 两个 ChatServer 设置不同 `CHAT_NODE_ID`，经 Nginx 分流登录 A/B，验证 Redis 跨节点单聊。
5. 心跳期间检查 `TTL nexusim:presence:user:<id>` 被续租；用旧 session 执行续租/删除脚本返回 0，当前 key 保持不变。
6. 停止 Redis 后登录应失败；已登录连接续租失败后应收到 `PRESENCE_LOST` 并断开。
7. 发送好友/群组消息类型，确认返回 `FEATURE_DISABLED` 且数据库没有变化。
