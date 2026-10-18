# NexusIM MPRPC integration

本目录保留施磊 MPRPC 课程工程的核心 `RpcChannel`、`RpcProvider`、Controller 和
ZooKeeper 注册/发现结构，并在 NexusIM Stage 3 中做增量加固。第三方来源和原始署名
应随项目继续保留。

当前实际构建入口是本目录的 `CMakeLists.txt` 或仓库根 `CMakeLists.txt`。课程版本的
`src/rpcheader.proto` 及其生成文件仅作为历史参考，不参与 Stage 3 目标；现行协议为：

```text
4-byte big-endian length + proto/rpc_envelope.proto
```

主要变化包括双向增量分帧、单一总 Deadline、结构化错误、RAII socket、ZooKeeper
有界等待和 Provider 有界业务执行器。完整线程和测试说明见
`../docs/stage3-mprpc-user-service.md`。
