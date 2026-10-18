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

Windows 开发机没有 WSL/Muduo/MySQL/hiredis Linux 开发环境；Linux 结果单独按目标机真实输出记录，不用 Windows 结果替代。

## Linux 实际测试记录（2026-10-18，北京时间）

目标机日志显示 GCC 15.2.0、OpenSSL 3.5.5。以下结果已经实际获得：

- `stage1_core_tests` 与 `stage1_security_tests`：2/2 通过，总计 0.20 秒（此数字仅是本次测试运行时间，不作为性能指标）。
- 首次 ChatServer 构建因缺少 `hiredis/hiredis.h` 失败；安装 hiredis 开发包后该错误消失。
- 随后构建暴露 `main.cpp` 依赖旧头文件全局 namespace 的错误，已由提交 `aedcd2f` 改成显式 `muduo::net::EventLoop/InetAddress`。
- ChatServer 可在 `127.0.0.1:6000` 启动，`server_frame_probe.py` 实际输出 `server_frame_probe: PASS`。
- 探针建立的四个短连接分别覆盖逐字节半包、粘包双帧、零长度非法帧和超限非法帧；服务端日志中的四次连接移除符合预期。
- 初次以 `Aoi_@localhost` 访问新库得到 MySQL 1044；授予该账户 `nexusim.*` 运行时 CRUD 权限后，`auth_probe.py` 连续两次通过，创建用户 id 2 和 3。
- 两次注册响应均为 `msgid=5, errno=0`，随后登录响应均为 `msgid=2, errno=0` 且返回 64 字符 session ID。数据库实查两个新密码均以 `pbkdf2_sha256$120000` 开头，连接关闭后状态均为 `offline`。
- `session_security_probe.py` 实际通过：未认证心跳和禁用接口检查成功；认证心跳返回 `msgid=12, ok=true`；用户 5 向用户 4 发送包含伪造身份的消息后，接收方看到的仍是服务端认证的用户 5 及其真实用户名，且消息中没有 `session_id`。
- `presence_probe.py` 使用用户 1 实际通过：登录获得 64 字符 session ID；Presence 初始 TTL 为 90 秒，等待后为 88 秒，认证心跳后恢复为 90 秒；错误 owner 的 compare-delete 返回 0，连接关闭后当前 Presence key 被安全删除。

尚未据此宣称通过：旧明文密码升级后的最终数据库字段确认、SQL 特殊字符专项黑盒测试、跨节点单聊和 Redis 故障恢复。

## Linux 构建与验证

```bash
cmake -S tests/stage1 -B build/stage1-tests -DCMAKE_BUILD_TYPE=Debug -DNEXUSIM_BUILD_SECURITY_TESTS=ON
cmake --build build/stage1-tests -j
ctest --test-dir build/stage1-tests --output-on-failure

cmake -S chatserver -B build/chatserver -DCMAKE_BUILD_TYPE=Debug
cmake --build build/chatserver -j
mysql -u Aoi_ -p < chatserver/sql/002_clone_chat_to_nexusim.sql
```

`002_clone_chat_to_nexusim.sql` 新建独立的 `nexusim` 数据库并从旧 `chat` 复制五张表，不会修改或删除 `chat`。目标表只要有任意数据，脚本就中止，避免重复迁移产生重复行。迁移后仅在新库把 `OfflineMessage` 改为自增 `id` 主键、`user_id` 普通索引和 `MEDIUMTEXT message`，从而支持同一用户的多条离线消息。若 `Aoi_` 没有创建数据库的权限，应由数据库管理员执行脚本，再查询 `mysql.user` 中 `Aoi_` 的实际 `Host` 并为该账户授予 `nexusim.*` 的运行权限。不要凭空假设账户 Host 为 `localhost` 或 `%`。

按 `chatserver/.env.example` 设置环境变量（特别是 `CHAT_DB_NAME=nexusim`），但不要提交实际密码。先启动 MySQL/Redis，再启动 ChatServer，执行：

```bash
python3 tests/stage1/server_frame_probe.py --host 127.0.0.1 --port 6000
python3 tests/stage1/auth_probe.py --host 127.0.0.1 --port 6000
python3 tests/stage1/existing_login_probe.py --host 127.0.0.1 --port 6000 --user-id 1
python3 tests/stage1/session_security_probe.py --host 127.0.0.1 --port 6000
python3 tests/stage1/presence_probe.py --host 127.0.0.1 --port 6000 --user-id 1
```

集成验收还必须人工或自动记录：

1. 旧密码被原样复制到新库以保持登录兼容；每个旧账号必须在隔离环境成功登录一次，确认 `nexusim.User.password` 随即变成 `pbkdf2_sha256$...`。新注册账号必须直接保存该格式；包含单引号的用户名和消息不得改变 SQL 结构。
2. 两个连接登录 A/B，A 发送伪造的 `id/name`，B 收到的发送者仍是 A 的认证身份。
3. 用代理延迟 MySQL/Redis 请求时，另一个连接仍能收到协议错误/业务响应；EventLoop 线程栈中不出现同步数据库、Redis 或 RPC 调用。
4. 两个 ChatServer 设置不同 `CHAT_NODE_ID`，经 Nginx 分流登录 A/B，验证 Redis 跨节点单聊。
5. 心跳期间检查 `TTL nexusim:presence:user:<id>` 被续租；用旧 session 执行续租/删除脚本返回 0，当前 key 保持不变。
6. 停止 Redis 后登录应失败；已登录连接续租失败后应收到 `PRESENCE_LOST` 并断开。
7. 发送好友/群组消息类型，确认返回 `FEATURE_DISABLED` 且数据库没有变化。
