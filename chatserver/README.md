# NexusIM ChatServer（阶段 2）

课程版 Muduo 集群聊天服务器的渐进式安全改造。阶段 1 的长度前缀分帧、有界业务执行器、连接会话身份、Redis Presence 安全续租/删除、PBKDF2 密码哈希和 V1 handler allowlist 均保留；阶段 2 增加按 node ID 的 Redis Pub/Sub 跨节点单聊和 Nginx stream 双节点入口。

当前没有 TLS，只允许隔离测试网络，不得开放公网。Redis Pub/Sub 不提供可靠投递；V1 尚无端到端 ACK、重试或多设备同步。构建、配置与真实测试边界见 [`../docs/stage2-cluster.md`](../docs/stage2-cluster.md)。
