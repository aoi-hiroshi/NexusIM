# NexusIM Stage 5 测试报告

## 1. 范围与环境

- 日期：2026-10-20
- Windows 开发机：Visual Studio 2019 x64，Qt 5.12.8 `msvc2017_64`
- 当前开发机没有 Linux 后端虚拟机，没有远程操作测试 PC。
- 本报告仅将当前机器上实际执行的结果标为 PASS。

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
| Linux ChatServer/AIService 构建 | NOT RUN | 待 Ubuntu 测试机 |
| 真实 DeepSeek API | NOT RUN | 待 Ubuntu 与用户 Key |
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

## 4. 待 Ubuntu 验收

1. 根 CMake 生成 Protobuf，构建 ChatServer、AIService 和 Stage 5 probes。
2. 用 loopback Mock 执行 `stage5_deepseek_client_probe` 所有错误分支。
3. 检查 ZooKeeper `/nexusim.ai.AIService/Chat -> 127.0.0.1:7002`。
4. 真实 API 单轮、多轮、双用户隔离与并发上限。
5. AI 慢请求期间的普通单聊、心跳和另一连接响应。
6. AIService 不可用、Key 缺失、429、5xx、HTTP/RPC/Qt 超时。
7. 断线/注销/换号后的迟到 AI 响应不投递。
8. Windows 两个 Release 客户端通过 `192.168.73.128:8000` 进行最终联调。

详细命令见 `docs/stage5-linux-testing.md`。
