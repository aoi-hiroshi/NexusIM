# NexusIM Stage 4 Windows 测试报告

## 1. 报告范围

- 日期：2026-10-19
- 工作区：Windows 本地 NexusIM 仓库
- 编译器：Visual Studio 2019，MSVC 19.29.30159，x64
- Qt：5.12.8 `msvc2017_64`
- CMake 生成器：Visual Studio 16 2019
- 被测对象：`client/qt` 独立桌面客户端

本报告只记录实际运行过的 Windows 编译、单元测试、Mock TCP、打包和启动结果。
本次 Windows 环境没有连接用户在 Linux 上运行的 NexusIM 后端，因此真实登录、跨节点聊天、
Nginx 分流和离线消息领取仍列为待联调，不以 Mock 结果替代。

## 2. 实际结果

| 项目 | 结果 | 实际证据 |
|---|---|---|
| Debug 构建 | PASS | 生成 `build/qt-client/bin/Debug/NexusIMClient.exe` 及 4 个测试程序。 |
| Debug CTest | PASS | 最终回归 4/4，通过时间 1.67 秒。 |
| Release 构建 | PASS | 生成 `build/qt-client/bin/Release/NexusIMClient.exe`。 |
| Release CTest | PASS | 最终回归 4/4，通过时间 1.48 秒。 |
| Stage 4 源码守卫 | PASS | 输出 `stage4_qt_source_guards: PASS`。 |
| Release 打包 | PASS | `windeployqt` 后存在 Qt5 Core/GUI/Network/Widgets、插件、VC142 CRT。 |
| Release 包启动 | PASS | 进程保持运行并创建标题为 `NexusIM` 的主窗口。 |
| 同机双进程启动 | PASS | 两个独立 PID 同时保持运行并各自创建 `NexusIM` 窗口。 |
| 登录页视觉检查 | PASS | 900×620 下 QSS、中文、账号/密码、连接状态与设置入口正常显示。 |
| Linux 真实服务联调 | NOT RUN | 本次未连接 Linux ChatServer，不能宣称通过。 |
| Windows 双端真实聊天 | NOT RUN | 已验证可双开，未验证两个真实账号收发。 |

Release 编译中出现的 C4127 告警位于 Qt 5.12.8 自带 `qvector.h` 模板内部；项目源码未产生
新的编译错误。它不影响本次测试结论，但升级 Qt 前应继续保留现有兼容性基线。

## 3. 自动化测试覆盖

### qt_framecodec_tests

- 4 字节网络字节序编码。
- 单帧、半包、粘包和多帧循环解析。
- 空 payload、零长度帧和超过 1 MiB 的非法帧。

### qt_protocol_tests

- Login、Register、DirectChat、Heartbeat 的实际 JSON 字段。
- `offlinemsg`/`friends` 的 JSON 字符串数组和对象数组兼容解析。
- 结构化服务端错误的可读文本生成。

### qt_requestmanager_tests

- Login/Register 这类无 `request_id` 旧协议的同类型单 Pending 限制。
- Heartbeat `request_id` 精确关联。
- `msgid=13` + `request_msgid` 错误匹配。
- 请求超时、单项取消、全部取消与迟到响应隔离。

### qt_networkclient_tests

- 真实本机 `QTcpServer`/`QTcpSocket` 异步连接。
- 半包后补齐以及连续多帧接收。
- 非法长度触发协议错误和断开。
- 未监听端口的连接拒绝错误。

### source_guards.py

- 固定 1 MiB 帧上限、持续接收缓冲和多帧解析循环。
- 仅使用异步 `connectToHost`/`readyRead`，禁止 `waitFor*` 阻塞 API。
- 30 秒 Heartbeat、密码暂存清理、群组/AI 真实能力声明。
- 禁止 Qt 密码日志和客户端单例锁标记，确保可以双开。

## 4. 可复现命令

```powershell
$cmake = "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$qt = "E:\Qt\Qt5.12.8\5.12.8\msvc2017_64"

& $cmake -S client/qt -B build/qt-client `
  -G "Visual Studio 16 2019" -A x64 `
  -DCMAKE_PREFIX_PATH="$qt" `
  -DNEXUSIM_QT_BUILD_TESTS=ON

& $cmake --build build/qt-client --config Debug --parallel
& $cmake --build build/qt-client --config Release --parallel

$env:Path = "$qt\bin;$env:Path"
ctest --test-dir build/qt-client -C Debug --output-on-failure
ctest --test-dir build/qt-client -C Release --output-on-failure
python client/qt/tests/source_guards.py

powershell -ExecutionPolicy Bypass -File client/qt/package.ps1 -Configuration Release
```

## 5. Linux/Windows 联调待验收项

待用户保持 Linux 后端运行并给出 Windows 可达的 IP 后，依照
`docs/stage4-qt-client.md` 完成：同节点 6001、跨节点 6001/6002、Nginx 8000、
重复登录、离线消息、服务中断与重连。每项应同时保存客户端现象和 ChatServer 节点日志。

当前结论：Stage 4 客户端源码、Windows 构建、自动化测试和发布包已完成；真实分布式后端
联调尚未执行，不能将 Stage 4 整体标记为端到端验收完成。
