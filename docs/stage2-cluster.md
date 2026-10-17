# NexusIM 阶段 2：Redis Pub/Sub + Nginx 双节点集群

## 1. 范围与基线

阶段 2 直接建立在提交 `c8acff7` 上，没有回退到 `64b75e4`，因此保留了
阶段 1 之后的 Muduo namespace、MySQL 实际表名、独立 `nexusim` 数据库迁移及
登录/会话/Presence 探针修复。本阶段不修改 `mprpc/`，也不开发 Qt 或 AIService。

本版本只适用于隔离测试网络。TCP 和 Redis 链路尚未配置 TLS，不得开放公网。

## 2. 运行关系

```text
CLI client --4-byte BE frame--> Nginx stream :8000
                                      | round-robin per TCP connection
                          +-----------+-----------+
                          v                       v
                 ChatServer-1 :6001      ChatServer-2 :6002
                 node=chatserver-1       node=chatserver-2
                          |                       |
                          +---- Redis :6379 ------+
                          | Presence + Pub/Sub    |
                          +------ MySQL :3306 ----+
```

每个节点有三个独立 hiredis 连接：

- Presence 连接：同步 `GET`、`SET NX EX` 和 compare-and-renew/delete Lua；由
  有界业务/心跳执行器调用，并由独立 mutex 串行化。
- Publish 连接：同步 `PUBLISH`；有独立 mutex，不与 Presence 互相阻塞。
- Subscribe 连接：只订阅 `nexusim:chat:node:<node_id>`。订阅线程只解析
  `ClusterDelivery` 并投递到 `redis-inbound` 有界执行器，不读取或修改连接表。

单线程 `redis-inbound` worker 按订阅连接的到达顺序处理 envelope，重新检查
`recipient_id + expected_session_id` 是否仍是本节点当前会话。它不执行数据库、
同步 Redis 或 RPC。最终的 `TcpConnection::send` 通过 `sendFramed()` 回投该连接
所属 EventLoop。订阅线程不会越线程直接操作 Muduo 连接；心跳另有独立执行器。

## 3. Presence 生命周期

共享 key：`nexusim:presence:user:<user_id>`。

值：`node_id|connection_id|session_id`，其中 `session_id` 是 64 个十六进制字符
（256 bit 随机值）。

1. 登录密码验证成功后，服务器执行 `SET key value NX EX ttl`。只有成功占用的会话
   才能绑定连接；其他节点上的重复登录返回 `errno=2`。
2. 已认证心跳绕过 connection strand，进入独立 Presence 执行器，通过 Lua 比较
   完整 value 后续租。未认证连接不能续租。
3. 注销或正常断线时，通过 Lua 比较完整 value 后删除；旧 session 无法删除新值。
4. 节点崩溃不会执行全局重置，key 最终由 TTL 过期。
5. Redis 或订阅连接不可用时，新登录失败关闭；续租失败返回 `PRESENCE_LOST`
   并关闭连接。

不同 ChatServer 必须使用不同的 `CHAT_NODE_ID`。相同 node ID 会导致两个订阅者
竞争同一节点频道；发布端会记录 subscriber count 异常，但这种配置不受支持。

## 4. DirectChat 路由

业务处理仍在 Stage 1 的 connection strand 上运行，因此同一连接的登录、单聊、
注销保持提交顺序；不同连接可并行。EventLoop 不执行 MySQL 或同步 Redis 命令。

1. 从服务端 `SessionContext` 取 sender id/name，覆盖客户端传入的 `id/name`。
2. 检查本节点连接表。命中时再次核对已认证会话，通过目标 EventLoop 发送。
3. 本地未命中时查询共享 Presence。
4. key 不存在：发送节点先通过类型校验后的数值 ID 查询确认目标用户存在，再单独写
   `OfflineMessage`；接收节点不写，避免重复入库。查询失败与用户不存在使用不同错误码。
5. key 指向其他节点：发布含目标 session 快照的 envelope 到该节点频道。
6. 接收节点再次核对当前本地 session，匹配后回投 EventLoop；过期 envelope 丢弃并
   记录日志。
7. Presence 查询失败、PUBLISH 失败或订阅者数为 0：发送节点尝试离线入库，并向
   发送者返回显式 `ERROR_MSG`（`PRESENCE_UNAVAILABLE` 或
   `REDIS_DELIVERY_FAILED`，同时给出 `queued_offline`）。数据库写失败返回
   `OFFLINE_STORE_FAILED`，不会宣称发送成功。

### V1 明确限制

Redis Pub/Sub 只有“发布时通知”，没有持久化、消费确认或端到端 ACK。`PUBLISH`
返回一个订阅者只证明 Redis 当时把消息交给了订阅连接，不能证明客户端已收到。
Presence 查询后到目标节点核对前仍有竞态；目标恰好断线时 envelope 会被丢弃。
目标节点的有界 inbound 队列满时也会记录错误并丢弃，而不会无限占用内存。
阶段 2 不实现重试、去重 ID、全局排序、多设备同步或 ACK 可靠投递。

## 5. Linux 依赖检查与构建

不要预设版本，先在目标机执行：

```bash
cmake --version
g++ --version
pkg-config --modversion hiredis || true
mysql_config --version || mariadb_config --version
ldconfig -p | grep -E 'muduo_(net|base)|hiredis|mysqlclient'
nginx -V 2>&1
redis-server --version
mysql --version
python3 --version
```

配置和构建：

```bash
cd /path/to/NexusIM/chatserver
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
# Optional staging install of the same configurable binary:
cmake --install build --prefix /opt/nexusim
```

现有工程把可执行文件输出到 `chatserver/bin/ChatServer` 和
`chatserver/bin/ChatClient`。若目标机的 MySQL 库不在 `/usr/lib64/mysql`，应按
实际 `mysql_config --libs` 调整链接路径，不要复制另一份源码。

数据库使用当前仓库的 `nexusim` schema。若仍只有原课程 `chat` 库，由管理员只执行
非破坏性的 `002_clone_chat_to_nexusim.sql`（它已经包含密码列与离线表升级）：

```bash
mysql -u <admin> -p < chatserver/sql/002_clone_chat_to_nexusim.sql
mysql -u <admin> -p -e "SHOW TABLES FROM nexusim; DESCRIBE nexusim.OfflineMessage;"
```

不要再对原 `chat` 库执行 `001_stage1_security.sql`。若 `nexusim` 已由阶段 1 建立，
不要重复运行 clone。管理员应按该账号真实的 MySQL `Host` 值授予 `nexusim.*` 上
V1 所需的 `SELECT, INSERT, UPDATE, DELETE` 权限；不要猜测 Host，也不要把命令中的
真实密码或生成的 `.env` 文件提交到仓库。

## 6. Redis 配置

`deploy/redis/redis-stage2.conf` 是单机隔离测试模板：

```bash
redis-server /path/to/NexusIM/deploy/redis/redis-stage2.conf
redis-cli -h 127.0.0.1 -p 6379 PING
```

生产式密码或 ACL 应放在服务器私有配置中，并把相同的
`CHAT_REDIS_USERNAME/CHAT_REDIS_PASSWORD` 注入两个实例。模板中的 AOF 只保护普通
key，不会让 Pub/Sub 可靠。两个实例必须连接同一 Redis 地址和 DB 命名空间。

检查频道与 Presence：

```bash
redis-cli PUBSUB NUMSUB nexusim:chat:node:chatserver-1 nexusim:chat:node:chatserver-2
redis-cli --scan --pattern 'nexusim:presence:user:*'
redis-cli GET nexusim:presence:user:<user_id>
redis-cli TTL nexusim:presence:user:<user_id>
```

启用 ACL 时可通过 `REDISCLI_AUTH` 和 `redis-cli --user <name>` 检查，避免把密码
直接留在 shell history。

## 7. 启动两个 ChatServer

复制模板并仅在目标机填写秘密：

```bash
cp deploy/config/chatserver-1.env.example deploy/config/chatserver-1.env
cp deploy/config/chatserver-2.env.example deploy/config/chatserver-2.env
chmod 600 deploy/config/chatserver-1.env deploy/config/chatserver-2.env
chmod +x deploy/scripts/*.sh tests/stage2/*.py
```

两个文件的 `CHAT_NODE_ID` 和 `CHAT_LISTEN_PORT` 必须分别为
`chatserver-1/6001`、`chatserver-2/6002`。同一主机启动：

```bash
deploy/scripts/start-chatservers.sh
ss -ltnp | grep -E ':6001|:6002'
tail -F run/chatservers/logs/chatserver-1.log run/chatservers/logs/chatserver-2.log
```

也可以传入自定义路径：

```bash
deploy/scripts/start-chatservers.sh /opt/nexusim/bin/ChatServer /etc/nexusim /var/run/nexusim
deploy/scripts/stop-chatservers.sh /var/run/nexusim
```

脚本不清空数据库，也不扫描或删除全部 Presence。进程崩溃时依赖 TTL 清理。

## 8. Nginx Stream

配置为 `deploy/nginx/nginx.conf`，使用 TCP stream round-robin，不解析也不改变
4 字节大端长度前缀 JSON。

检查 stream 支持：

```bash
nginx -V 2>&1 | grep -- --with-stream
```

部分发行版把 stream 构建为动态模块。若上式未显示静态支持，安装发行版对应的
stream 模块包，并按 `nginx -V`/包文档在主配置顶层加载
`ngx_stream_module.so`；不要盲写固定模块路径。

验证并启动独立配置：

```bash
sudo nginx -t -c "$PWD/deploy/nginx/nginx.conf"
sudo nginx -c "$PWD/deploy/nginx/nginx.conf"
ss -ltnp | grep ':8000'
sudo tail -F /var/log/nginx/nexusim-stream-access.log /var/log/nginx/nexusim-error.log
```

重载/停止时继续传同一个绝对配置路径：

```bash
sudo nginx -s reload -c "$PWD/deploy/nginx/nginx.conf"
sudo nginx -s quit -c "$PWD/deploy/nginx/nginx.conf"
```

## 9. 测试

### 平台无关测试（Windows VS2019 或 Linux）

```powershell
cmake -S tests/stage1 -B out/stage1 -G "Visual Studio 16 2019" -A x64
cmake --build out/stage1 --config Release
ctest --test-dir out/stage1 -C Release --output-on-failure

cmake -S tests/stage2 -B out/stage2 -G "Visual Studio 16 2019" -A x64
cmake --build out/stage2 --config Release
ctest --test-dir out/stage2 -C Release --output-on-failure
python tests/stage2/source_guards.py
python -m compileall -q tests/stage2
```

### Linux 真实集群

服务全部启动后：

```bash
python3 tests/stage2/cluster_probe.py
```

该探针真实创建临时用户，依次验证本节点单聊、双向跨节点单聊、重复登录、心跳、
一次离线入库以及 Nginx 是否把长连接分配到两个 node。没有 Nginx 时只能显式跳过：

```bash
python3 tests/stage2/cluster_probe.py --skip-nginx
```

复用阶段 1 的 Presence 精确删除测试：

```bash
python3 tests/stage1/presence_probe.py --port 6001 --user-id <id>
```

Redis 故障测试会在安全位置暂停，要求操作者确认 Redis 已停止；它验证错误响应、
离线回退标记、响应上界和心跳 fail-closed。完成后重新启动 Redis和两个 ChatServer：

```bash
python3 tests/stage2/redis_failure_probe.py
```

节点崩溃 TTL 测试同样不主动 kill 服务。先准备一个可登录账号，再按提示对拥有该
会话的节点执行 `kill -KILL <pid>`：

```bash
python3 tests/stage2/presence_crash_probe.py --port 6001 --user-id <id>
```

协议粘包、半包、多帧和非法帧继续使用 Stage 1 的真实服务器探针：

```bash
python3 tests/stage1/server_frame_probe.py --port 6001
python3 tests/stage1/session_security_probe.py --port 6001
```

## 10. 验收记录边界

Windows 仅能真实验证不依赖 Muduo/hiredis/MySQL/Nginx 的 C++ 协议单测、Stage 1
公共组件回归、Python 语法和源码线程边界 guard。双 ChatServer、Redis、MySQL、
Nginx 与故障注入结果必须在 Linux 目标机实际执行后记录；未执行前不得标为通过。

### 2026-10-18 Windows 实际结果

- 发现的工具链：VS2019 Community，MSVC `19.29.30159.0`，Windows SDK
  `10.0.19041.0`。CMake 不在 PATH，实际使用 VS 自带
  `CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe`。
- Qt 安装目录实际存在于 `E:\Qt\Qt5.12.8`；本阶段没有编译或修改 Qt 客户端。
- Stage 1 公共回归：`stage1_core_tests` 1/1 通过（含半包、逐字节、粘包多帧、
  非法长度、跨连接进展、心跳独立执行器与 PendingRequest 生命周期）。
- Stage 2 协议测试：`stage2_cluster_protocol_tests` 1/1 通过。
- `stage2_source_guards.py`：通过。
- `python -m compileall -q tests/stage2`：通过。
- 未在 Windows 声称通过：ChatServer Linux 链接、真实 MySQL/Redis、双节点 Pub/Sub、
  Nginx stream、Redis 停机与节点崩溃 TTL。这些均待 Linux 实机按本章执行。
