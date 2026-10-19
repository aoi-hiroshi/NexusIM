# Stage 4 Qt 客户端协议与功能矩阵

本文件按当前 `chatserver/include/public.hpp`、`ChatServer`、`ChatService` 和
Stage 1 `FrameCodec` 的真实实现整理。Qt 客户端没有新增或重编号消息类型。

## 传输层

- 每条消息为 `4 字节网络字节序无符号长度 + UTF-8 JSON payload`。
- 长度必须为 `1..1 MiB`；零长度和超长帧视为协议错误并断开连接。
- TCP 可出现半包、粘包及一次到达多帧；客户端使用持久 `QByteArray` 循环解析。
- 当前 V1 未启用 TLS，只允许在隔离测试网络使用，不应开放公网。

## 消息矩阵

| msgid | 名称/方向 | 主要 JSON 字段 | 当前状态及客户端行为 |
|---:|---|---|---|
| 1 | Login，请求 | `id:int`、`password:string` | 开放。仅未认证连接可调用。密码不记录日志。 |
| 2 | LoginAck，响应 | `errno:int`；成功时 `id`、`name`、`session_id`、`node_id`，可选 `offlinemsg`、`friends`；失败时可有 `error`、`errmsg` | 开放。旧协议无 `request_id`，客户端限制同一时刻只能存在一个 Login/对应响应请求。 |
| 3 | Logout，请求 | 无额外字段 | 开放。无成功 ACK；服务端安全释放 Presence 后关闭连接。 |
| 4 | Register，请求 | `name:string`、`password:string` | 开放。名称 1–50 UTF-8 字节；密码 8–128 UTF-8 字节。 |
| 5 | RegisterAck，响应 | `errno:int`；成功时 `id:int`；失败时可有 `error`、`errmsg` | 开放。旧协议无 `request_id`，客户端串行化同类请求。 |
| 6 | DirectChat，请求/推送 | 请求：`toid:int`、`msg:string`、`time:string`。推送另含服务端覆盖的 `id:int`、`name:string` | 开放。发送者身份只取已认证连接上下文。没有成功 ACK、送达或已读回执；socket 写入仅显示为本地已发送。 |
| 7 | AddFriend，请求 | 旧课程字段不作为 V1 公共契约 | `FEATURE_DISABLED`，Qt 联系人页不提供伪成功操作。 |
| 8 | CreateGroup，请求 | 旧课程字段不作为 V1 公共契约 | `FEATURE_DISABLED`，入口保留并明确未开放。 |
| 9 | AddGroup，请求 | 旧课程字段不作为 V1 公共契约 | `FEATURE_DISABLED`。 |
| 10 | GroupChat，请求/推送 | 旧课程字段不作为 V1 公共契约 | `FEATURE_DISABLED`。 |
| 11 | Heartbeat，请求 | `request_id:string` | 开放且要求已认证。客户端每 30 秒发送，最长等待 10 秒。 |
| 12 | HeartbeatAck，响应 | `request_id:string`、`ok:bool`，失败可有 `error` | 开放。按 `request_id` 精确匹配；Presence 丢失时服务端发送失败响应并关闭连接。 |
| 13 | Error，响应 | `error:string`、`errmsg:string`，业务错误可含 `request_msgid`、`node_id`、`toid`、`queued_offline` | 开放。客户端使用 `request_msgid` 匹配 Login/Register/Heartbeat 错误；DirectChat 错误显示在对应会话。 |

## 服务端主动数据

- 实时单聊使用 `msgid=6` 推送。客户端按服务端给出的发送者 `id/name` 归入会话。
- 登录成功响应中的 `offlinemsg` 是 JSON 字符串数组（兼容对象数组），不是完整历史记录。
- 登录成功响应中的 `friends` 是 JSON 字符串数组（兼容对象数组）。客户端展示 ID 与名称，
  不把旧 `state` 字段当作可信的实时在线状态。
- 服务端不会主动推送好友状态、已读状态或完整历史。

## 鉴权、错误与兼容规则

- Login/Register 只能在未认证连接使用；DirectChat/Logout/Heartbeat 均要求认证。
- 客户端从不发送或相信自填 `sender_id/name/session_id`；`DirectChat` 只发送目标和正文。
- Login/Register 没有 `request_id`，`RequestManager` 对相同请求/响应类型只允许一个 Pending；
  本地超时或取消后会关闭旧 TCP 连接，防止迟到响应被下一次登录误认。
- Heartbeat 有关联 ID；结构化 `Error` 通过 `request_msgid` 回配。
- DirectChat 成功无 ACK。Redis/Presence 故障时服务端可能返回 `queued_offline=true`，这只表示已排队到
  离线表，不表示实时送达。客户端不会显示“已送达”或“已读”。
- 离线消息没有稳定 `message_id`。客户端只在单次登录响应内对完整 JSON 做 SHA-256 去重，
  无法承诺跨登录的幂等去重。

## 功能覆盖

| 功能 | Stage 4 状态 | 说明 |
|---|---|---|
| 登录、注册、注销 | 已实现 | 使用真实 1–5 号协议及超时/错误处理。 |
| 单聊发送与接收 | 已实现 | 支持多会话、未读计数、中文/英文/Emoji/多行文本。 |
| 离线消息 | 已实现兼容解析 | 明确标记离线消息；不称为聊天历史。 |
| 好友列表 | 只读展示 | 数据来自 LoginAck；添加好友仍被服务端禁用。 |
| 按用户 ID 会话 | 已实现 | 服务端没有公开 QueryUser 客户端接口，名称未知时显示 `用户 #ID`。 |
| 群组/群聊 | 禁用占位 | 后端安全 allowlist 未开放 8–10 号 handler。 |
| AI 助手 | Stage 5 占位 | 没有发送模型请求，也不模拟回复。 |
| 历史、ACK、已读、多设备 | 未实现 | 超出 V1/Stage 4 范围。 |
