# Stage 4：Qt 5 桌面客户端实现与验证

## 实现范围

Stage 4 在 `client/qt` 新增独立的 Qt 5.12.8/C++17 Windows 工程，不改变
Stage 1–3 的 ChatServer、MPRPC 或 UserService。模块边界如下：

- `protocol/`：4 字节大端 FrameCodec 和现有 JSON 消息适配。
- `network/`：异步 `QTcpSocket`、累积解析、连接超时和 Pending 请求管理。
- `controllers/`：认证、会话、心跳、消息路由和 UI 状态协调。
- `models/`：会话及内存消息模型；不伪造服务端历史或在线状态。
- `ui/`：登录、注册、设置、三栏主窗口、会话列表及消息气泡。
- `resources/`：浅色 QSS；群组和 AI 页面明确标注后端未开放/Stage 5。

每个进程独立拥有 `QTcpSocket`、`RequestManager`、Session 和 ConversationModel，
没有单例互斥，可直接双开。网络 API 全部使用 signal/slot；不存在
`waitForConnected`、`waitForReadyRead` 或 UI 线程中的同步数据库/RPC 调用。

## 交互与可靠性边界

- 登录和注册期间禁用提交按钮，连接拒绝、超时、协议错误和服务端业务错误均显示可读信息。
- 登录后每 30 秒发独立 Heartbeat；它不等待聊天请求，响应以 `request_id` 匹配。
- 断线会取消所有 Pending、清除认证态和会话模型，并返回登录页；重新点击登录即手动重连。
- Login/Register 旧协议没有关联 ID；若本地超时或取消，客户端会关闭旧 TCP 流，隔离迟到响应。
- 输入 Enter 发送，Ctrl+Enter 保留为换行；正文限制 65536 UTF-8 字节。
- 发送成功只表示整帧已进入本机 socket 缓冲区，不表示对端已收到。V1 无 ACK、重试、已读和全局排序。
- 离线消息只是登录时领取的数据，不是完整历史；旧协议缺少 message ID，去重范围有限。
- 服务器入口通过设置页或 `--host/--port` 配置，不写死 VMware 地址。
- V1 无 TLS，只能在隔离测试网络使用。

完整字段见 `docs/stage4-protocol-matrix.md`。

## Windows 构建

已选用本机实际环境：Qt `5.12.8 msvc2017_64` + Visual Studio 2019 x64。
Qt 的 msvc2017_64 ABI 可由 VS2019 工具链兼容使用。

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$qt = "E:\Qt\Qt5.12.8\5.12.8\msvc2017_64"

& $cmake -S client/qt -B build/qt-client `
  -G "Visual Studio 16 2019" -A x64 `
  -DCMAKE_PREFIX_PATH="$qt" `
  -DNEXUSIM_QT_BUILD_TESTS=ON

& $cmake --build build/qt-client --config Release --parallel
```

该独立工程不会在 Windows 查找 Muduo、hiredis、ZooKeeper、MySQL 或 Linux MPRPC。

## 自动化测试

```powershell
$env:Path = "E:\Qt\Qt5.12.8\5.12.8\msvc2017_64\bin;$env:Path"
ctest --test-dir build/qt-client -C Release --output-on-failure
python client/qt/tests/source_guards.py
```

测试覆盖：

- `qt_framecodec_tests`：编码、半包、粘包、多帧、零长度和超长帧。
- `qt_protocol_tests`：Login/Register/DirectChat/Heartbeat JSON 及字符串对象数组解析。
- `qt_requestmanager_tests`：响应类型、Heartbeat 关联 ID、结构化错误、超时、取消和旧回复隔离。
- `qt_networkclient_tests`：本地 `QTcpServer` 下半包/多帧、非法帧断开和连接拒绝。
- `source_guards.py`：异步 socket、1 MiB 上限、心跳、禁用页、无阻塞 socket API、无单例锁和密码日志检查。

## 打包

先完成 Release 构建，再运行：

```powershell
powershell -ExecutionPolicy Bypass -File client/qt/package.ps1 -Configuration Release
```

默认输出 `dist/NexusIMClient-Release/`。脚本调用 Qt 5.12.8 的 `windeployqt`，并检查
`Qt5Core.dll`、`vcruntime140.dll` 和 `platforms/qwindows.dll`。普通 PowerShell 未设置
`VCINSTALLDIR` 时，脚本会自动加载本机 VS2019 x64 Developer 环境。

## Linux 后端和 Windows 双端验收

先在 Linux 启动并确认 MySQL、Redis、ZooKeeper、UserService、ChatServer-1/2 和
Nginx Stream。Windows 防火墙/VM 网络需允许访问 Linux 的 6001、6002、8000，
但数据库、Redis、ZooKeeper 和 UserService 不应暴露给 Windows 客户端。

启动两个进程（示例地址需替换）：

```powershell
Start-Process dist\NexusIMClient-Release\NexusIMClient.exe -ArgumentList "--host","<linux-ip>","--port","6001"
Start-Process dist\NexusIMClient-Release\NexusIMClient.exe -ArgumentList "--host","<linux-ip>","--port","6002"
```

按顺序验收并保存服务端日志：

1. A、B 分别登录；同账号在第二端登录应被拒绝，第一端保持在线。
2. 6001 同节点双向发送中文、英文、Emoji、连续消息、空行和较长文本。
3. A=6001、B=6002 双向发送，服务端日志确认 Redis 跨节点路径。
4. 两端均改为 Nginx 8000，重新登录并发送；日志确认连接确实分布到两个节点。
5. B 离线时 A 发送，B 再登录后收到标记为“离线消息”的记录。
6. 停止 ChatServer、使用错误地址、断网、请求超时，确认客户端提示且不崩溃；恢复后重新登录。
7. 观察群组和 AI 页面只显示未开放信息；不得出现模拟成功或模拟 AI 回复。

## 2026-10-19 Windows 本机验证记录

| 验证项 | 状态 | 证据/边界 |
|---|---|---|
| VS2019 x64 Debug 编译 | 通过 | `NexusIMClient.exe` 与 4 个 QtTest 可执行文件均生成。 |
| Debug CTest | 通过 | 4/4，FrameCodec、协议、RequestManager、Mock TCP 全部通过。 |
| Debug `windeployqt` | 通过 | Qt Debug DLL 与 `platforms/qwindowsd.dll` 已生成；普通终端报告 `VCINSTALLDIR` 未设置。 |
| 客户端启动与界面检查 | 通过 | 登录页在 900×620 下启动，QSS、中文、连接设置和状态显示正常。 |
| Release 编译/CTest | 通过 | VS2019 x64 生成 Release EXE；最终回归 4/4 CTest 通过，总计 1.48 秒。 |
| Release 打包与启动 | 通过 | Qt DLL、qwindows 插件及 VC142 Runtime 齐全；打包 EXE 创建 `NexusIM` 窗口。 |
| 同机双开能力 | 通过 | 两个 Release 进程使用独立 PID 同时运行；未使用单例锁。 |
| Linux ChatServer 真实连接 | 待 Windows/Linux 联调 | 当前 Windows 测试没有目标后端连接证据。 |
| Windows 双客户端真实单聊 | 待 Windows/Linux 联调 | 不以 Mock TCP 或双进程启动替代真实聊天结果。 |

完整实际输出结论见 `docs/stage4-test-report.md`。
