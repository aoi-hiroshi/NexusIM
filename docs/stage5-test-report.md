# NexusIM Stage 5 测试报告

## 1. 范围与环境

- 日期：2026-10-20
- Windows 开发机：Visual Studio 2019 x64，Qt 5.12.8 `msvc2017_64`
- Windows 开发机没有直接执行 Linux 后端命令；Ubuntu 结果来自用户在测试
  虚拟机逐项执行并回传的原始终端输出。
- 本报告只将 Windows 实际执行结果或已提供完整终端证据的 Ubuntu 项目标为
  PASS；尚未执行的 Qt 双端 UI 验收继续标为 NOT RUN。

## 2. Windows 实际结果

| 项目 | 结果 | 证据 |
|---|---|---|
| Stage 5 portable Debug 构建 | PASS | VS2019 生成 `stage5_ai_core_tests.exe` |
| AI 核心 CTest | PASS | 1/1，0 failed，0.16 s |
| Stage 5 源码守卫 | PASS | `stage5_source_guards: PASS` |
| Qt Debug 构建 | PASS | 生成 `NexusIMClient.exe` 及 6 个 QtTest |
| Qt Debug CTest | PASS | 6/6，0 failed，5.43 s |
| Stage 4 Qt 守卫回归 | PASS | `stage4_qt_source_guards: PASS` |
| Qt Release 构建/CTest | PASS | 6/6，0 failed，5.51 s |
| Release 打包与启动 | PASS | `windeployqt` 成功，打包进程保持运行 |
| Mock HTTP 服务脚本语法 | PASS | Python `py_compile` |
| Mock HTTP Unicode 请求 | PASS | loopback POST 200，`id=mock-request-id` |
| Linux 启停脚本语法 | PASS | MSYS2 Bash `-n` |
| C++ DeepSeekClient 对 Mock HTTP | NOT RUN | Windows 本机未配置 libcurl C++ 开发包 |
| Linux ChatServer/UserService/AIService 构建 | PASS | Ubuntu 实机构建成功；修复 AIService 的 bundled `json.hpp` include 路径后生成三个可执行文件 |
| 真实 DeepSeek API | PASS | AIService RPC 返回 `status=0`、HTTP 200；ChatServer 端到端见下表 |
| Windows 双客户端 Stage 5 端到端 | NOT RUN | 待用户在测试 PC 验收 |

## 3. 自动化覆盖

`stage5_ai_core_tests` 验证：

- 200 正常、UTF-8/换行、usage、request ID 与长文本解析。
- 401/403、429、500、非法 JSON、空 choices、空 content 和 `finish_reason=length`。
- `user_id + conversation_id` 隔离、同会话单在途请求、多轮顺序。
- 失败请求不提交历史、历史轮数上限和非活跃 LRU 容量淘汰。

QtTest 新增验证：

- AI JSON 字段与 `14/15` 消息 ID，客户端不发送 `user_id/session_id`。
- 多个已关联请求可并存，响应按 request ID 精确匹配；旧认证协议仍单在途。
- 通过本地 `QTcpServer` 验证 AIController 的真实分帧发送、成功回复关联、
  新会话取消 Pending 与忙状态复位。
- Stage 1 分帧、Stage 4 协议/RequestManager/NetworkClient 全部回归。

`mock_deepseek_server.py` 可模拟 normal、401、429、500、超时、非法
JSON、空 choices、空 content、长文本、中文/特殊字符和上下文回显。脚本输出始终明确标识为
Mock，不伪装真实 DeepSeek 回复。

## 4. Ubuntu 实际结果

测试机于 2026-10-20 完成以下实测；数值仅代表本次功能验证，不作为性能
基准：

| 项目 | 结果 | 实际证据 |
|---|---|---|
| 根 CMake 与 Linux 目标构建 | PASS | GCC 15.2、Protobuf 3.21.12、OpenSSL 3.5.5、libcurl 8.18.0；ChatServer、UserService、AIService 与 probes 均链接成功 |
| Linux CTest | PASS | 3/3，0 failed，0.08 s |
| Stage 5 源码守卫 | PASS | `stage5_source_guards: PASS` |
| ZooKeeper AI 注册 | PASS | `/nexusim.ai.AIService/Chat -> 127.0.0.1:7002` |
| AIService 真实 DeepSeek RPC | PASS | `status=0`、`model=deepseek-v4-pro`、HTTP 200、退出码 0 |
| ChatServer-1 真实 AI | PASS | 1.358 s，59 content bytes |
| ChatServer-2 真实 AI | PASS | 1.240 s，26 content bytes |
| Nginx 8000 真实 AI | PASS | 1.220 s，26 content bytes |
| Mock libcurl 错误矩阵 | PASS | 10/10：normal、Unicode、300000-byte long、401、429、500、invalid JSON、empty choices/content、timeout |
| 多轮上下文 | PASS | context Mock 的第二轮包含两轮 marker |
| AIService 并发限制 | PASS | `AI_MAX_CONCURRENT_REQUESTS=1` 时 success=1、busy=1 |
| 慢 AI 同连接心跳 | PASS | 三秒 Mock 期间 heartbeat 约 0.001 s，AI 最终成功 |
| 慢 AI 跨连接聊天 | PASS | 跨节点单聊约 0.001 s，AI 最终成功 |
| 连接生命周期 | PASS | 注销/重登后旧 AI 结果未进入新 session |
| AIService 不可用 | PASS | 返回 `AI_SERVICE_UNAVAILABLE`；同时 Stage 2 集群全项通过 |
| API Key 缺失 | PASS | 返回 `AI_CONFIG_MISSING` |
| Stage 1/2 最终回归 | PASS | FrameCodec、连接身份、Redis 跨节点、Presence、离线单存储、Nginx 3/3 均通过 |

测试期间还观察到：长时间运行的 UserService 在 ZooKeeper session 永久过期后
仍监听 7001，但临时服务节点已消失；重启 UserService 可恢复注册。自动创建新
session 并重新注册服务节点尚未实现，因此不得将“端口监听”单独作为 RPC 健康
依据。

## 5. 待验收与已知限制

1. Windows 两个 Release 客户端通过隔离测试网的 Nginx 8000 完成 AI 页面、
   普通聊天并发、错误展示、清空/新对话及注销迟到响应验收。
2. 补充 ZooKeeper session 永久过期后的自动重新注册，并执行停启 ZooKeeper
   的恢复测试；修复前由启停脚本重启 UserService/AIService 恢复。
3. 客户端 TCP 链路仍无 TLS，只允许隔离测试网络使用。

详细复现命令见 `docs/stage5-linux-testing.md`。
