# NexusIM Stage 6 Linux 构建与验收

以下命令在 Ubuntu 仓库根目录执行。Windows 开发机没有运行 Linux 后端，因此本文
中的 Linux 项目必须由测试机真实执行；不要把源码守卫或 Windows 单元测试记为
集群联调成功。

## 1. 更新与数据库升级

先备份数据库。确认当前库为 `nexusim`，不要在课程原始 `chat` 库上执行升级。

```bash
cd "$HOME/NexusIM/NexusIM"
git pull --ff-only

mysql -h 127.0.0.1 -u nexusim -p nexusim -e '
SELECT user_id,friend_id,COUNT(*) c FROM Friend
GROUP BY user_id,friend_id HAVING c>1;
SELECT group_id,user_id,COUNT(*) c FROM GroupUser
GROUP BY group_id,user_id HAVING c>1;'
```

两个查询都应返回空集。若有重复，先人工确认数据；不要盲目删除。确认后运行：

```bash
mysql -h 127.0.0.1 -u nexusim -p nexusim \
  < chatserver/sql/003_stage6_relationship_constraints.sql
```

验证索引：

```bash
mysql -h 127.0.0.1 -u nexusim -p nexusim -e '
SHOW INDEX FROM Friend WHERE Key_name="uq_friend_user_pair";
SHOW INDEX FROM GroupUser WHERE Key_name="uq_group_user_pair";'
```

## 2. 构建

```bash
cmake --fresh -S . -B build/stage6 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/stage6 -j"$(nproc)" \
  --target ChatServer UserService AIService stage6_protocol_tests
```

旧版 CMake 不支持 `--fresh` 时，删除一个专用且已确认路径的 `build/stage6` 后重新
配置，或使用新的构建目录；不要删除仓库根目录。

## 3. 启动顺序

```bash
sudo systemctl start mysql
sudo systemctl start redis-server
sudo systemctl start zookeeper

deploy/scripts/stop-user-service.sh
deploy/scripts/stop-ai-service.sh
deploy/scripts/stop-chatservers.sh

deploy/scripts/start-user-service.sh
deploy/scripts/start-ai-service.sh
deploy/scripts/start-chatservers.sh
sleep 5
```

若 Nginx 已按 Stage 2 配置运行，无需重启；否则使用已经验证过的 stream 配置启动。
检查：

```bash
ss -ltnp | grep -E ':6001|:6002|:7001|:7002|:8000'
redis-cli PUBSUB NUMSUB \
  nexusim:chat:node:chatserver-1 \
  nexusim:chat:node:chatserver-2
```

两个 Redis 节点频道的订阅数都应为 1。

## 4. 自动测试

```bash
ctest --test-dir build/stage6 --output-on-failure
python3 tests/stage6/source_guards.py
python3 tests/stage6/social_group_probe.py \
  --host 127.0.0.1 --node1-port 6001 --node2-port 6002
```

`social_group_probe.py` 会创建四个临时账号，真实检查：双向好友及幂等、建群事务、
入群幂等、成员权限、同节点/跨节点群聊、伪造身份覆盖、单次离线群消息、普通成员
退出和群主退出限制。

Redis 故障注入需要一个至少有两名成员的真实群，且其他成员离线或不在发送节点
本地在线。先登录发送者，再按提示停止 Redis：

```bash
python3 tests/stage6/group_redis_failure_probe.py \
  --host 127.0.0.1 --port 6001 \
  --user-id <群成员ID> --password '<密码>' --group-id <群ID>
```

通过标准是响应在有界时间返回、`redis_published=0`、
`realtime_routed=false`，并明确给出离线入库或失败计数。完成后启动 Redis 并重启
ChatServer，使 hiredis 命令连接和订阅恢复。

## 5. Stage 1–5 回归

```bash
python3 tests/stage1/server_frame_probe.py --host 127.0.0.1 --port 6001
python3 tests/stage1/session_security_probe.py --host 127.0.0.1 --port 6001
python3 tests/stage2/cluster_probe.py
python3 tests/stage5/ai_chat_probe.py \
  --host 127.0.0.1 --port 8000 \
  --user-id <真实测试用户ID> --password '<真实测试密码>'
```

Stage 1 的 session probe 已更新：7 号 AddFriend 不再期待 `FEATURE_DISABLED`，而是
验证未认证连接收到 16 号 Ack 和 `UNAUTHENTICATED`。

## 6. 双 Qt 客户端验收

1. A 直连 6001，B 直连 6002；双方按 ID 添加好友并刷新。
2. A 创建群，B 按群 ID 加入；两端打开群成员列表。
3. 两端互发中文和长文本群消息，确认用户名正确且无重复。
4. 第三个成员离线时发送一条群消息，重新登录后确认只恢复一次，并显示在对应群。
5. 用非成员账号尝试发群消息，确认 UI 展示 `NOT_GROUP_MEMBER`。
6. 停 Redis 后发送跨节点群消息，确认没有崩溃，Ack 不声称全部实时送达。
7. 同时进行 AI 长请求、心跳和另一连接群聊，确认其他连接仍及时响应。

保存终端输出、服务日志和 Qt 截图后再将 Linux/双端项目标记为 PASS。
