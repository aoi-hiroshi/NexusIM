# NexusIM ChatServer（阶段 1）

课程版 Muduo 集群聊天服务器的渐进式安全改造。阶段 1 已加入长度前缀分帧、有界业务执行器、连接会话身份、Redis Presence 安全续租/删除、PBKDF2 密码哈希和 V1 handler allowlist。

当前没有 TLS，只允许隔离测试网络，不得开放公网。Redis Pub/Sub 不提供可靠投递；V1 尚无端到端 ACK、重试或多设备同步。构建、配置与真实测试边界见 [`../docs/stage1-implementation.md`](../docs/stage1-implementation.md)。
