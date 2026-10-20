# NexusIM Stage 5 实现说明

## 1. 实现范围

Stage 5 在现有 ChatServer、MPRPC 和 Qt 5.12.8 客户端上增量实现，没有重写原课程框架。

- 独立 `AIService` Linux 进程，经 ZooKeeper 注册 `nexusim.ai.AIService/Chat`。
- ChatServer 新增独立有界 AI RPC 执行器，长请求不占用 EventLoop 或普通 connection strand。
- `DeepSeekClient` 使用 libcurl HTTPS，默认调用 `deepseek-v4-pro` 非流式接口。
- Qt 新增 `AIController` 和 `AIChatWidget`，仍只连接 ChatServer。
- 实现有界内存多轮上下文；AIService 重启后历史丢失。
- 没有实现流式 Token、RAG、MCP、持久化 AI Memory 或全局 ACK。

## 2. 调用链与线程边界

```text
Qt AIChatWidget
  -> AIController / RequestManager (request_id)
  -> QTcpSocket + 4-byte big-endian JSON frame
  -> Nginx stream
  -> ChatServer dispatchAiChat
  -> dedicated bounded ai-rpc executor
  -> MPRPC framed request + total deadline
  -> AIService RpcProvider bounded worker
  -> ConversationManager per-conversation lease
  -> DeepSeekClient/libcurl HTTPS
  -> RPC response
  -> PendingRequest lifecycle + connection/session recheck
  -> TcpConnection owner EventLoop queued send
  -> Qt request_id + conversation_id recheck
```

`AI_CHAT_MSG=14`，`AI_CHAT_MSG_ACK=15`。客户端请求只包含
`request_id`、`conversation_id` 和 `prompt`，不接受客户端传入的
`user_id`。ChatServer 从已认证连接上下文取得用户身份。

AI 处理在解析完 JSON 后、进入普通 connection strand 前被分流。
因此同一连接等待 AI 时，普通单聊仍可进入 strand，心跳仍走独立
Presence 执行器。AI 执行器队列满时立即返回 `AI_SERVICE_BUSY`。

## 3. 生命周期与会话顺序

- ChatServer PendingRequest 绑定 `connection_id + session_id + request_id + deadline`。
- 断线、注销或换号会取消该 session 的 PendingRequest。
- RPC 返回后重新核对当前 session；迟到结果不会投递给新会话。
- AIService 以 `user_id + conversation_id` 隔离历史。
- 同一 AI 会话仅允许一个在途请求，通过 RAII lease 串行化上下文提交。
- 只有模型成功回复才提交 user/assistant 整轮，超时或错误不写入历史。
- Qt 再次核对 `request_id + conversation_id`，客户端超时或切换会话后的回复被忽略。

## 4. 超时与容量

| 层级 | 默认值 | 配置 |
|---|---:|---|
| DeepSeek 连接 | 5 s | `DEEPSEEK_HTTP_CONNECT_TIMEOUT_MS` |
| DeepSeek 整体 HTTP | 75 s | `DEEPSEEK_HTTP_TIMEOUT_MS` |
| ChatServer AI RPC | 85 s | `CHAT_AI_RPC_DEADLINE_MS` |
| Qt 等待 | 90 s | `AIController` 构造参数 |
| ChatServer AI worker/queue | 4 / 64 | `CHAT_AI_EXECUTOR_THREADS/QUEUE` |
| AIService 业务并发 | 4 | `AI_MAX_CONCURRENT_REQUESTS` |
| AIService Provider worker/queue | 6 / 64 | `MPRPC_PROVIDER_THREADS/QUEUE` |
| HTTP 响应体 | 512 KiB | `DEEPSEEK_MAX_RESPONSE_BYTES` |
| 单次 prompt | 32 KiB | `AI_MAX_PROMPT_BYTES` |

下游 HTTP 超时短于 RPC，RPC 又短于 Qt 等待。V1 不强制中断已在 libcurl
内执行的请求，但完成后必须通过上述生命周期检查才可回投。

## 5. DeepSeek 配置与安全

DeepSeek 默认配置使用 `https://api.deepseek.com/chat/completions`、
`stream=false`、`thinking.type=disabled`、`reasoning_effort=none`。模型、思考
开关和 token 上限均由环境变量控制。

- API Key 仅从 `DEEPSEEK_API_KEY` 读取。
- libcurl 强制 `CURLOPT_SSL_VERIFYPEER=1` 和 `CURLOPT_SSL_VERIFYHOST=2`。
- 仅在显式设置 `DEEPSEEK_ALLOW_HTTP_MOCK=true` 时允许 loopback HTTP Mock。
- 日志记录 request/user/耗时/HTTP 状态/token，不记录 Key、Authorization
  或完整私密对话。
- 未配置 Key 时 AIService 仍能启动和注册，调用返回
  `AI_CONFIG_MISSING`；普通聊天不受影响。

当前 V1 的 ChatServer 客户端 TCP 链路未启用 TLS，仅可在隔离测试网络中使用，
不得直接暴露到公网。

## 6. Qt 交互

- 左侧 AI 入口显示 DeepSeek AI 对话页。
- Enter 发送，Ctrl+Enter 换行；请求中禁止重复点击。
- 等待动画、耗时、模型名称、截断警告和结构化错误均在 UI 显示。
- “清空显示”只清本地气泡；“新对话”换用新 `conversation_id`。
- Qt 不包含 DeepSeek URL 或 API Key，也不使用阻塞式 `waitFor*` 网络 API。

## 7. 错误映射

401/403 → `AI_AUTH_FAILED`，429 → `AI_RATE_LIMITED`，HTTP 5xx/DNS/SSL →
`AI_UPSTREAM_ERROR`，HTTP 超时 → `AI_UPSTREAM_TIMEOUT`，结构不完整 →
`AI_INVALID_RESPONSE`，ZooKeeper/服务节点不存在 → `AI_SERVICE_UNAVAILABLE`。

Stage 5 的官方 API 字段及模型配置以 DeepSeek 官方 API 文档为准：

- <https://api-docs.deepseek.com/api/create-chat-completion/>
- <https://api-docs.deepseek.com/guides/thinking_mode/>
