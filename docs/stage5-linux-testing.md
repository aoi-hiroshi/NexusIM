# NexusIM Stage 5 Linux 构建与联调

## 1. 边界

本文命令在 Ubuntu 测试虚拟机执行。示例虚拟机地址为
`192.168.73.128`；它不写入任何 C++ 源码。Windows 客户端优先连接
`192.168.73.128:8000`。当前 TCP 链路未配置 TLS，仅用于隔离的 VMware
测试网络。

## 2. 依赖与构建

Stage 5 开始将带真实口令的 `deploy/config/*.env` 从 Git 跟踪中移除。
首次拉取本提交前，先在 Linux 上备份现有本地配置：

```bash
mkdir -p "$HOME/.config/nexusim"
cp deploy/config/chatserver-1.env "$HOME/.config/nexusim/"
cp deploy/config/chatserver-2.env "$HOME/.config/nexusim/"
# git pull --ff-only 之后，如工作树中已不存在则恢复：
cp "$HOME/.config/nexusim/chatserver-1.env" deploy/config/
cp "$HOME/.config/nexusim/chatserver-2.env" deploy/config/
chmod 600 deploy/config/chatserver-1.env deploy/config/chatserver-2.env
```

本地 `.env` 现由 `.gitignore` 保护，恢复后不会再出现在 `git status`。

```bash
sudo apt update
sudo apt install -y build-essential cmake protobuf-compiler libprotobuf-dev \
  libssl-dev libcurl4-openssl-dev ca-certificates \
  libzookeeper-mt-dev zookeeperd libhiredis-dev \
  default-libmysqlclient-dev redis-server mysql-server \
  nginx libnginx-mod-stream

# Muduo 仍使用 Stage 1-4 已安装的实际版本。
cd "$HOME/NexusIM/NexusIM"
cmake --fresh -S . -B build/stage5 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/stage5 -j"$(nproc)" --target \
  ChatServer UserService AIService \
  stage3_rpc_frame_tests stage3_rpc_protocol_tests \
  stage5_ai_core_tests stage5_ai_service_rpc_probe \
  stage5_deepseek_client_probe
```

只有当命令实际成功时，才能记录 Linux 构建为 PASS。

## 3. 配置

```bash
cp deploy/config/ai-service.env.example deploy/config/ai-service.env
chmod 600 deploy/config/ai-service.env
nano deploy/config/ai-service.env
```

在本地秘密文件中填入 `DEEPSEEK_API_KEY`。`deploy/config/*.env` 已被
`.gitignore` 排除，不要将 Key 写到 `.env.example`、日志或 shell 命令历史。

两个 ChatServer 的本地 `.env` 需各自增加：

```env
CHAT_AI_RPC_DEADLINE_MS=85000
CHAT_AI_EXECUTOR_THREADS=4
CHAT_AI_EXECUTOR_QUEUE=64
```

若要从 Windows 直连 6001/6002 调试，在隔离网络中把
`CHAT_LISTEN_IP` 设为 `0.0.0.0`；否则保持 `127.0.0.1` 并只通过 Nginx 8000
访问。

## 4. 网络与证书检查

```bash
getent hosts api.deepseek.com
curl -I --connect-timeout 5 --max-time 15 https://api.deepseek.com
openssl s_client -connect api.deepseek.com:443 \
  -servername api.deepseek.com -verify_return_error </dev/null
```

HTTP 404/405 不代表 TLS 失败；重点检查 DNS、TLS 验证和能否建立 HTTPS
连接。不得使用 `-k` 或关闭 libcurl 证书验证来“修复”问题。

## 5. 启动顺序

```bash
cd "$HOME/NexusIM/NexusIM"
chmod +x deploy/scripts/*.sh

sudo systemctl start mysql
sudo systemctl start redis-server
sudo systemctl start zookeeper

deploy/scripts/start-user-service.sh
deploy/scripts/start-ai-service.sh
deploy/scripts/start-chatservers.sh

# Nginx 已按 Stage 2 启动则无需重复启动。
sudo nginx -t -c /tmp/nexusim-nginx.conf
sudo nginx -c /tmp/nexusim-nginx.conf
```

Nginx stream 配置不需因 AIService 修改；它只在 6001/6002 之间分配客户端
TCP 连接。

## 6. 启动验证

```bash
ss -ltnp | grep -E ':6001|:6002|:7001|:7002|:8000'

/usr/share/zookeeper/bin/zkCli.sh -server 127.0.0.1:2181 \
  get /nexusim.ai.AIService/Chat

tail -n 100 run/ai-service/logs/ai-service.log
tail -n 100 run/chatservers/logs/chatserver-1.log
tail -n 100 run/chatservers/logs/chatserver-2.log
```

ZooKeeper 节点应返回 `127.0.0.1:7002`。SLF4J binder 提示不影响 C 客户端
注册结果。

## 7. Mock HTTP 测试

本测试只验证 HTTP/解析/错误映射，不是真实 DeepSeek 结果。

```bash
python3 tests/stage5/mock_deepseek_server.py \
  --host 127.0.0.1 --port 18080 --scenario normal

export DEEPSEEK_API_KEY=mock-only
export DEEPSEEK_BASE_URL=http://127.0.0.1:18080
export DEEPSEEK_ALLOW_HTTP_MOCK=true
export DEEPSEEK_MODEL=deepseek-v4-pro
export DEEPSEEK_THINKING=disabled
export DEEPSEEK_REASONING_EFFORT=none
build/stage5/tests/stage5/stage5_deepseek_client_probe AI_OK
```

分别以 `401`、`429`、`500`、`timeout`、`invalid-json`、`empty-choices`、
`empty-content`、`long`、`unicode` 重启 Mock，并验证对应状态。
`context` 场景会将收到的 user messages 标记到回复中，用于验证多轮上下文。
`stage5_deepseek_client_probe` 的调用 deadline 固定为 2000 ms；因此使用默认
三秒 `timeout` Mock 时预期状态是 `AI_UPSTREAM_TIMEOUT`，不是 `AI_OK`。

## 8. 真实 API 与 RPC 测试

```bash
ctest --test-dir build/stage5 --output-on-failure
python3 tests/stage5/source_guards.py

# 使用 ai-service.env 的真实 Key 启动 AIService 后：
set -a
source deploy/config/ai-service.env
set +a
build/stage5/tests/stage5/stage5_ai_service_rpc_probe
echo "exit_code=$?"
```

RPC probe 只有在返回 `AI_STATUS_OK` 和退出码 0 时才是真实 API PASS。

经 ChatServer 的黑盒探针：

```bash
python3 tests/stage5/ai_chat_probe.py \
  --host 127.0.0.1 --port 6001 \
  --user-id <id> --password '<password>'

# 以 timeout Mock 重启 AIService 后执行，验证同连接心跳不被 AI 阻塞：
python3 tests/stage5/ai_nonblocking_probe.py \
  --host 127.0.0.1 --port 6001 \
  --user-id <id> --password '<password>'

# 以延迟 Mock 执行，--wait-seconds 应大于 Mock 延迟：
python3 tests/stage5/ai_lifecycle_probe.py \
  --host 127.0.0.1 --port 6001 \
  --user-id <id> --password '<password>' --wait-seconds 4

# context Mock：
python3 tests/stage5/ai_multiturn_probe.py \
  --host 127.0.0.1 --port 6001 \
  --user-id <id> --password '<password>'

# AI_MAX_CONCURRENT_REQUESTS=1 + slow Mock：
python3 tests/stage5/ai_concurrency_probe.py \
  --host 127.0.0.1 --port 6001 \
  --first-user-id <id-a> --first-password '<password-a>' \
  --second-user-id <id-b> --second-password '<password-b>'
```

## 9. Windows 双客户端验收

1. 运行两份 Release 包，均配置 `192.168.73.128:8000`。
2. A/B 使用不同账号登录，先回归跨节点单聊。
3. A 完成 AI 单轮和依赖上文的第二轮。
4. A/B 同时使用 AI，确认上下文不串话。
5. A 等待 AI 时由 B 发单聊，确认单聊和心跳仍正常。
6. 停止 AIService，确认 Qt 显示 `AI_SERVICE_UNAVAILABLE`，但单聊仍正常。
7. 将 Key 临时留空、使用 Mock 429/500/超时，逐项保存客户端与服务日志。
8. AI 请求未返回时注销，然后换号登录，确认旧结果不显示。
9. 分别直连 6001/6002 发起 AI，确认都调用同一独立 AIService。
10. 使用 `windeployqt` 的 Release 包复测，不依赖 Qt Creator。

## 10. 故障排查

- `AI_SERVICE_UNAVAILABLE`：检查 7002、AIService 日志和 ZooKeeper znode。
- `AI_CONFIG_MISSING`：检查进程实际加载的 `ai-service.env`，不要打印 Key。
- `AI_AUTH_FAILED`：在 DeepSeek 控制台检查 Key/权限，不要关闭 TLS。
- `AI_UPSTREAM_TIMEOUT`：检查虚拟机外网、DNS 和三层超时大小。
- 启动时 znode 存在：等待旧 ZooKeeper session 在有界时间内失效，不要无条件删除别的实例节点。
- 进程仍监听但 RPC 显示 `get failed: no node`：检查日志是否出现 ZooKeeper
  `session ... has expired`。当前 V1 需重启相应 UserService/AIService 以建立新
  session 并恢复临时节点；不要仅用监听端口判断服务健康。
