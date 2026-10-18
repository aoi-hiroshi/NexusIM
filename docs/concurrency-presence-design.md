# V1 并发、连接生命周期与 Presence 设计约束

本文是后续业务改造的强制约束。若实现与本文冲突，应先更新设计和测试，再修改代码。

## 1. EventLoop 边界

Muduo EventLoop 只允许执行：

- 接受/关闭连接；
- 从 Buffer 增量解帧；
- 基础协议校验与报文大小检查；
- 将业务任务提交给有界执行器；
- 在原连接所属 EventLoop 中发送最终响应。

以下操作不得直接运行在 EventLoop 回调中：

- MySQL 查询和写入；
- Redis 同步命令；
- ZooKeeper 查询或连接；
- MPRPC 同步调用；
- HTTP/LLM 请求；
- 可能等待锁、磁盘或外部网络的工作。

业务队列达到上限时立即返回 `SERVER_BUSY`，不得让 EventLoop 等待空闲槽位。

## 2. 异步 RPC 的连接生命周期

V1 的“异步 RPC”指：MPRPC 底层仍可使用同步调用，但同步调用只在独立 RPC/业务工作线程中运行，绝不阻塞 ChatServer EventLoop。

请求流程：

1. EventLoop 解出完整客户端帧并完成大小、类型和登录状态检查。
2. 为请求生成 `PendingRequest`，记录 `request_id`、`connection_id`、认证后的 `session_id`（未认证登录/注册使用随机 `lifecycle_id`）、原 I/O Loop 和 deadline。
3. 把任务提交到有界工作线程。
4. 工作线程执行带 connect/read/write 总超时的 MPRPC 调用。
5. 工作线程可以执行有界的 RPC、MySQL 和 Redis 条件操作，但不得直接修改连接表或认证 Session；Presence 占用必须携带本次随机 `session_id`，并在 Session 提交失败时比较后回滚。
6. RPC 完成后，通过原 I/O Loop 的 `queueInLoop/runInLoop` 回投完成事件。
7. I/O Loop 再次检查：连接仍存在、仍处于 connected 状态、`lifecycle_id/session_id` 未变化、请求尚未取消；超时 RPC 只允许返回明确超时错误，不能提交业务结果。
8. 只有全部检查通过才发送响应；迟到结果直接丢弃并记录原因。

断开连接时：

- SessionRegistry 立即撤销该 `connection_id + session_id`。
- 所有 PendingRequest 标记为 cancelled。
- 已进入同步 MPRPC 的系统调用不要求强杀线程，但 socket deadline 必须保证它在有限时间内返回。
- RPC 任务不能通过持有永久强引用让 TcpConnection 无限存活。允许为了单次有界任务短暂持有引用，但发送前仍必须按 session 二次校验。

重新连接会获得新的 `connection_id` 和 `session_id`。旧请求即使稍后成功，也不能写入新连接。

## 3. 同一连接的消息顺序

TCP 只保证字节顺序，不保证将业务帧自动分开。FrameCodec 必须先按 Buffer 顺序生成完整帧。

同一客户端连接使用串行任务队列（connection strand）：

- 每个连接最多有一个客户端请求正在执行业务逻辑。
- 后续完整帧按接收顺序排队。
- 当前任务完成或超时后才启动下一任务。
- 不同连接可并行执行，并共享固定大小的工作线程池。

这样保证登录、登出、单聊等同一连接请求不会因线程池调度而乱序，也避免“登录尚未完成，后续消息先通过鉴权”的竞态。

服务端主动推送统一回投接收方的 I/O Loop。V1 只保证单节点内提交到同一连接输出队列的顺序；Redis Pub/Sub 跨节点消息不承诺全局严格顺序。

为避免恶意连接无限排队，每连接和全局队列都必须配置最大任务数/字节数。超过限制返回 `TOO_MANY_REQUESTS` 或断开连接。

## 4. Redis Presence 数据模型

V1 单用户只允许一个有效会话。

键：

```text
nexusim:presence:user:<user_id>
```

值至少包含：

```text
node_id | connection_id | session_id
```

约束：

- `session_id` 由服务端使用安全随机源生成，至少 128 bit。
- 客户端不能指定或覆盖 `session_id`。
- `node_id` 来自服务端配置，在两个 ChatServer 间必须唯一。
- Presence 不保存密码或认证 token。

## 5. 登录占用

UserService 验证密码成功后，ChatServer 使用原子命令：

```text
SET key value NX EX <ttl>
```

成功才绑定本地连接。失败表示已有活跃会话，V1 返回重复登录错误，不执行强制顶号。

如果 Redis 不可用，集群模式下登录应失败并返回 `PRESENCE_UNAVAILABLE`，不能静默退化为可能重复登录的状态。

## 6. TTL 与续租

建议初始值：

- 客户端心跳间隔：30 秒；
- Presence TTL：90 秒。

最终数值必须通过集成测试确认，而不是写成性能结论。

每次心跳或定时续租都必须比较当前 value 与本会话 value。禁止简单执行 `EXPIRE key ttl`，否则旧连接可能给新会话续命。

续租 Lua 逻辑：

```lua
if redis.call('GET', KEYS[1]) == ARGV[1] then
    return redis.call('EXPIRE', KEYS[1], ARGV[2])
end
return 0
```

续租返回 0 表示 Presence 已不属于本 session，本地连接应进入失效流程，不能继续代表该用户发送消息。

## 7. 安全删除

断开、注销和登录失败回滚时禁止无条件 `DEL`。旧连接可能在新会话建立后才触发断开回调，无条件删除会误删新会话。

删除 Lua 逻辑：

```lua
if redis.call('GET', KEYS[1]) == ARGV[1] then
    return redis.call('DEL', KEYS[1])
end
return 0
```

只有 `node_id + connection_id + session_id` 完全匹配时才删除。

## 8. Redis Pub/Sub 与已知限制

V1 使用 Redis Pub/Sub 作为实时跨节点通知，不把它描述为可靠队列。

- 本地连接存在时直接发送。
- Presence 指向其他节点时发布带目标用户 ID 的消息。
- 订阅节点只向本地已认证且 session 有效的目标连接发送。
- 非目标节点不得重复写离线消息。
- Redis 中断、Presence 陈旧或订阅窗口丢失时，消息可能丢失。

由于 V1 不实现 ACK 和可靠重试，这些限制必须保留在 README 和演示说明中。

## 9. 旧业务接口限制

安全修复以网络可达面为边界，而不是以 Qt UI 是否展示为边界。

在完成鉴权、输入校验和 SQL 安全审计前，旧业务 handler 不得继续公开：

- 添加好友；
- 创建群；
- 加入群；
- 群聊；
- 任何仍信任客户端 `id/name` 的接口。

阶段 1 默认采用消息类型 allowlist，只注册已完成安全审计的 Login、Register、Logout、Heartbeat、DirectChat；QueryUser 在 UserService 阶段完成后再加入，AI 接口也在对应阶段完成审计后再加入。其他旧消息类型返回 `FEATURE_DISABLED`，而不是继续调用旧 Model。

## 10. 非 TLS 部署边界

如果 V1 不实现 TLS：

- 仅允许隔离测试网络；
- Nginx 入口不得映射公网地址；
- 使用防火墙限制来源；
- 数据库、Redis、ZooKeeper 和 RPC 端口只在内部网络开放；
- README 必须明确客户端密码和消息在传输层未加密；
- 不得宣称适用于生产或公网环境。

数据库中的用户密码仍必须使用带盐 PBKDF2 等抗暴力哈希保存；数据库账号和 LLM API Key 不得进入源码或 Git。
