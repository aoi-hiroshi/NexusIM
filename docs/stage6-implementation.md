# NexusIM Stage 6 实现说明

## 范围

Stage 6 在现有 Muduo ChatServer、Redis 节点频道、MySQL Model 和 Qt 5.12.8
客户端上增量开放好友、群组和群聊能力。没有重写 FrameCodec、MPRPC、Presence、
AIService，也没有提前引入 Docker Compose。

## 协议

所有报文继续使用 `4 字节网络字节序长度 + UTF-8 JSON`。7–10 号保留课程工程
请求 ID，16–27 号是 Stage 6 新增的业务响应和查询接口。

| ID | 名称 | 方向 | 关键字段 |
|---:|---|---|---|
| 7 | AddFriend | C→S | `request_id`, `friendid` |
| 8 | CreateGroup | C→S | `request_id`, `groupname`, `groupdesc` |
| 9 | JoinGroup | C→S | `request_id`, `groupid` |
| 10 | GroupChat | 双向 | 请求：`request_id`, `groupid`, `msg`；推送额外包含可信 `id`, `name`, `time` |
| 16 | AddFriendAck | S→C | `ok`, `already_friends`, `friend` |
| 17/18 | QueryFriends/Ack | C↔S | Ack 返回 `friends` 对象数组 |
| 19 | CreateGroupAck | S→C | `group`（ID、名称、描述、角色） |
| 20 | JoinGroupAck | S→C | `group_id`, `already_member` |
| 21/22 | QueryGroups/Ack | C↔S | Ack 返回 `groups` 对象数组 |
| 23/24 | QueryGroupMembers/Ack | C↔S | 仅成员可查，返回 `members` |
| 25/26 | LeaveGroup/Ack | C↔S | 普通成员退出；创建者拒绝退出 |
| 27 | GroupChatAck | S→C | 本地、Redis、离线和失败计数 |

所有 Stage 6 请求都要求 1–128 字节的受限 `request_id`。Ack 的 `request_id`
与请求一致。这里的 Ack 只表示业务处理结果，不是端到端可靠送达、已读或消息确认。

## 好友调用链

1. Qt `SocialController` 发送 AddFriend。
2. ChatServer 的 connection strand 顺序执行 `ChatService::addFriend`。
3. 身份只取认证 `SessionContext`，客户端传入的 `id` 不参与授权。
4. 通过 `UserService.QueryUser` 验证目标用户。
5. `FriendModel::addBidirectional` 在一个事务中补齐 A→B 和 B→A 两行。
6. `INSERT IGNORE` 配合 `(user_id, friend_id)` 唯一键保证重试幂等。
7. Qt 收到 Ack 后重新 QueryFriends，并更新联系人页面和单聊显示名称。

V1 仍是“直接添加”模型，不包含好友申请审批、备注名、黑名单和好友在线推送。
联系人页不会把 MySQL `state` 当作真实 Presence，因此显示“状态未知”。

## 群组调用链

- 创建群：`AllGroup` 插入与创建者 `GroupUser` 记录在同一事务中提交；角色为
  `creator`。
- 加群：先验证 `AllGroup`，再幂等写入 `GroupUser`，普通成员角色为 `normal`。
- 列表：只查询当前认证用户实际加入的群。
- 成员：先验证查询者属于该群，再返回 ID、名称和角色，不返回密码或 Session。
- 退群：普通成员可退出；创建者返回 `GROUP_OWNER_CANNOT_LEAVE`。Stage 6 不实现
  群主转让和解散。

## 分布式群聊路由

发送节点先查询群成员并确认发送者是成员，然后为除发送者外的每个目标执行：

1. 本节点存在当前认证连接：`sendFramed` 回投该连接 EventLoop。
2. Presence 指向其他节点：按目标用户及其期望 `session_id` 发布到
   `nexusim:chat:node:<node_id>`。
3. Redis 订阅线程只把信封交给有界 `redis-inbound` 执行器；接收节点再次核对
   `recipient_id`、消息类型、`toid` 和当前 Session，再回投 EventLoop。
4. 无 Presence、Presence 查询失败或发布失败：只由发送节点写入
   `OfflineMessage`，订阅节点绝不补写，避免重复。

群消息中的 `id`、`name` 始终由服务端 Session 覆盖；客户端无法伪造发送者。
正文限制为 65536 个 UTF-8 字节。V1 默认单次广播最多 256 名接收者，可用
`CHAT_GROUP_MAX_RECIPIENTS` 在 1–1000 范围内调整；超限会明确拒绝。离线副本使用
一个数据库连接和事务批量写入，避免逐成员反复建连。数据库、Redis 和 RPC 均在有界业务线程中执行，
Heartbeat 仍走独立 Presence 执行器，不会排在长群聊任务之后。

### 可靠性边界

- Redis Pub/Sub 仍是非持久实时通知，不提供端到端 Ack、重试或全局排序。
- Presence 查到远端且 Redis `PUBLISH` 成功后，若目标会话恰好断开，消息可能落入
  订阅窗口；Stage 6 不声称这种竞态下可靠送达。
- Presence 不存在、Redis 查询/发布明确失败时会尝试离线落库，并在 GroupChatAck
  返回 `queued_offline`、`failed` 和 `realtime_routed`，不会伪装成全部实时送达；
  `delivery_guaranteed` 在 V1 固定为 `false`。
- 登录返回的 `offlinemsg` 是一次性离线队列，不是完整历史记录。

## 数据库安全

`MySQL::executePrepared/queryPrepared` 统一封装 `MYSQL_STMT` 参数绑定。当前公开路径
中的 User、Friend、Group 和 OfflineMessage 输入不再通过字符串拼接进入 SQL。
建群使用事务；好友和群成员关系使用唯一约束保证并发幂等。

升级脚本：`chatserver/sql/003_stage6_relationship_constraints.sql`。它只增加唯一索引，
不会 DROP/TRUNCATE/覆盖表或删除行。执行前必须先检查历史重复关系；若存在重复，
由维护者确认并清理后再添加唯一键。

实际表名保持：`User`、`Friend`、`AllGroup`、`GroupUser`、`OfflineMessage`。

## Qt 客户端

- `SocialController`：封装请求关联、好友/群列表、群成员、群消息和错误处理。
- 联系人页：真实好友列表、本地姓名/ID 筛选、按 ID 添加、刷新、双击单聊。
- 群组页：真实群列表、创建、加入、退出、刷新、双击进入群聊。
- `GroupChatWidget`：成员列表、发送者名称、本人/他人气泡、中文长文本、离线标记、
  自动滚动和未选中群的新消息计数。
- QWidget 不直接处理 TCP 分帧、Redis 或数据库；所有网络操作继续异步经过
  `NetworkClient` 和 `RequestManager`。

## 未实现

好友申请审批、删除好友、群主转让/解散、管理员/踢人、消息已读/撤回、可靠 ACK、
多设备同步、持久历史、全局群消息顺序和 Redis Pub/Sub 丢失重试均不在 Stage 6。
