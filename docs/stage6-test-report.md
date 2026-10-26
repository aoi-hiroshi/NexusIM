# NexusIM Stage 6 测试报告

报告日期：2026-10-20

## 当前结论

Stage 6 源码实现和 Windows 可执行测试已完成。Windows Qt 5.12.8 + VS2019 Release
构建通过，8/8 个 QtTest 通过，可移植 Stage 6 协议测试通过，现有 Stage 2–6 源码守卫
通过。由于当前开发机没有运行 Ubuntu 后端，MySQL/Redis/Muduo 双节点真实业务测试
明确标记为 **待 Linux 实机验证**。

## Windows 实际结果

| 项目 | 结果 | 真实输出/说明 |
|---|---|---|
| Qt 5.12.8 Release 构建 | PASS | 生成 `build/qt-stage6/bin/Release/NexusIMClient.exe` |
| Qt FrameCodec | PASS | `qt_framecodec_tests` |
| Qt 协议 | PASS | 覆盖好友/群组请求、可信身份字段缺省、ID 兼容 |
| Qt RequestManager | PASS | 原请求关联回归 |
| Qt NetworkClient | PASS | 原粘包/半包网络回归 |
| Qt AIController | PASS | Stage 5 回归 |
| Qt SocialController | PASS | 离线群消息恢复到对应群会话 |
| Qt 好友/群组 UI | PASS | 真实控件、列表数据和群会话选择 |
| Qt AIChatWidget | PASS | Stage 5 UI 回归 |
| QtTest 汇总 | PASS | `100% tests passed, 0 failed out of 8` |
| Stage 6 portable protocol | PASS | `stage6_protocol_tests: PASS` |
| Stage 2–6 source guards | PASS | Stage2/3/4/5/6 守卫全部通过 |
| Python 探针语法 | PASS | Stage 6 probe 与更新后的 session probe 可编译 |

构建命令：

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' `
  -S client/qt -B build/qt-stage6 -G 'Visual Studio 16 2019' -A x64 `
  -DCMAKE_PREFIX_PATH='E:\Qt\Qt5.12.8\5.12.8\msvc2017_64' `
  -DNEXUSIM_QT_BUILD_TESTS=ON
& 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' `
  --build build/qt-stage6 --config Release --parallel 4
```

测试时将 `E:\Qt\Qt5.12.8\5.12.8\msvc2017_64\bin` 加入当前进程 PATH，然后执行
Release 配置的 CTest。

## Linux 待验证（2026-10-20 原始记录）

以下项目尚未在本轮开发机实际运行，因此当前不能记为 PASS：

- ChatServer/UserService/AIService 的 Linux Stage 6 全量编译。
- `003_stage6_relationship_constraints.sql` 在现有真实数据上的执行。
- 双向好友、幂等添加及重登录持久化。
- 建群事务、入群幂等、成员列表和退出规则。
- 同节点、跨节点、三成员和离线群消息。
- Redis 故障时群消息降级和明确 Ack。
- Windows 双 Qt 客户端与 Ubuntu 双节点联调。
- Stage 1–5 的 Linux 最终回归。

可复现命令和判定标准见 `docs/stage6-linux-testing.md`。Linux 用户回传真实输出后，
应在本报告追加“Linux 实机结果”，而不是覆盖本节的历史记录。

## Linux 实机结果（2026-10-26 补测）

在 Ubuntu 26.04 VMware 虚拟机的独立 Git worktree 中，基于 `1ec4727` 加本次
MySQL prepared SELECT 兼容修复构建；原课程快照与原工作目录的本地文件未被覆盖。
原生 `ChatServer`、`UserService`、`AIService` 均编译通过，CTest 5/5 通过。
首次构建遇到 `mysql_stmt_get_result` 未声明；改用 MySQL C API 的
`mysql_stmt_bind_result`、`mysql_stmt_fetch` 和 RAII 结果对象后重新构建成功。

| 真实测试 | 结果 | 证据 |
|---|---|---|
| 原生双节点好友/群组探针 | PASS | 好友双向及幂等、建群/入群/查询/权限、同节点和跨节点群聊、可信发送者身份、单次离线存储、退群规则；`group_id=1` |
| 容器内同一探针 | PASS | 首次 `group_id=1`，服务恢复后 `group_id=3`；最终故障后回归 `group_id=5` |
| Redis 容器中断时群聊 | PASS | `stage6_group_redis_failure_probe: PASS seconds=0.050 queued=1 failed=0`；恢复 Redis 与两个 ChatServer 后健康检查通过 |
| Stage 1/2 回归 | PASS | 分帧、会话鉴权、本节点/跨节点单聊、重复登录、Presence、Nginx 分流均通过 |
| 真实 DeepSeek 回归 | PASS | 原生请求约 1.690 秒；容器入口最终请求约 1.365 秒，均返回完整文本 |

既有 MySQL `Friend(user_id, friend_id)` 和 `GroupUser(group_id, user_id)`
已经各有复合主键，原生库无需重复添加相同唯一约束。新建容器数据库执行了
`001-schema.sql` 和 `003_stage6_relationship_constraints.sql`，随后完成上述黑盒测试。

本轮没有使用两台 Windows Qt 客户端进行 Stage 6 图形界面联调；这一项仍待人工验收。
以上秒数是单次故障/请求的观测值，不是性能基准。

## 已知限制

业务 Ack 不是可靠送达 Ack；Redis Pub/Sub 订阅窗口仍可能丢消息；离线队列不是历史
记录；没有好友审批/删除、群主转让/解散、管理员、已读/撤回、多设备同步或全局群
消息排序。V1 未启用 TLS 时仍仅允许在隔离测试环境运行。
