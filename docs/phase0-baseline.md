# 阶段 0：源码与环境基线

记录日期：2026-10-17

## 1. 源码来源快照

本仓库从当前工作区导入以下两套课程工程：

- `chatserver-master/chatserver-master` → `NexusIM/chatserver`
- `mprpc项目代码/mprpc` → `NexusIM/mprpc`

导入原则：

- 保留头文件、实现、CMake、示例、Proto 和必要测试源码。
- 不导入已有 `build/`、`lib/`、Linux 可执行文件、对象文件和测试二进制。
- 暂不重排 ChatServer 和 MPRPC 内部目录。
- 后续修改先以小步提交进行，不全面重写课程核心代码。

当前来源目录没有 Git 元数据，因而无法恢复上游历史提交。需要后续补充课程源码的准确仓库地址和许可证。已知内嵌 `nlohmann/json 3.6.1` 自带 MIT 许可证；这不代表两套课程工程整体采用 MIT。

## 2. Windows 环境实测

已验证：

- Windows Qt：5.12.8
- Qt 安装目录：`E:/Qt/Qt5.12.8`
- V1 客户端选用：`5.12.8/msvc2017_64`
- Qt Widgets、Qt Network、Qt Core、`windeployqt.exe` 均存在。
- Visual Studio：2019 Community
- MSVC x64：19.29.30159
- CMake：3.20.21032501-MSVC_2
- Ninja：1.10.2
- 备用 Qt MinGW：7.3.0 64 位

项目后续使用 Qt 5.12.8 `msvc2017_64` 与 VS2019 x64，不依赖全局 PATH 中的 qmake。

## 3. Linux 环境状态

本机没有可用 WSL 或 Docker 命令。SSH 配置中的目标检查结果：

- `10.101.16.26`：SSH 端口可达，但非交互认证失败。
- `10.101.26.26`：连接超时。
- `192.168.73.128`：连接超时。

根据最新实施约定，Codex 负责本地代码；Linux 编译、部署和最终打包由项目所有者把代码上传至 Linux 后执行。因此阶段 0 不声称 Muduo、MPRPC、MySQL、Redis、ZooKeeper 或 Protobuf 已在 Linux 上编译通过。

## 4. 本地 CMake 配置记录

在阶段 0 被中断前，曾使用 VS2019 CMake 对两套原工程执行配置生成。两者均生成了 Visual Studio 工程文件，说明旧 CMake 脚本可完成基础 configure；没有执行实际编译或链接。

该结果不能代表 Linux 构建成功，也不能代表 Muduo/MPRPC 可以在 Windows 上运行。生成目录 `.phase0-build/` 已被 `.gitignore` 排除，不进入版本库。

## 5. 阶段 0 完成条件

- 原始源码快照进入独立 Git 仓库。
- 构建产物与密钥被 `.gitignore` 排除。
- Qt/VS 环境记录基于真实命令结果。
- 异步 RPC、连接顺序、Presence 生命周期及旧接口限制形成强制设计文档。
- Linux 构建命令和结果记录格式准备完成，但不伪造未执行结果。
