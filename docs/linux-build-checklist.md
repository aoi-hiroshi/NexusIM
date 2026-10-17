# Linux 原始工程构建检查清单

阶段 0 不在 Windows 上伪造 Linux 构建结果。将代码上传到目标 Linux 后，按本文执行并把原始输出、修复和最终结果写入后续测试报告。

## 1. 环境信息

```bash
set -u
date -Iseconds
cat /etc/os-release
uname -a
g++ --version
cmake --version
make --version
protoc --version || true
pkg-config --version || true
```

## 2. 依赖检查

```bash
pkg-config --modversion protobuf || true
pkg-config --modversion hiredis || true
pkg-config --modversion libcurl || true
pkg-config --modversion openssl || true
pkg-config --modversion mysqlclient || true
pkg-config --modversion mariadb || true

ldconfig -p | grep -E 'muduo_(net|base)|zookeeper_mt|protobuf|hiredis|mysqlclient|mariadb|curl|ssl' || true

find /usr/local /usr -maxdepth 4 \
  \( -name 'libmuduo_net.so*' -o -name 'libmuduo_base.so*' -o -name 'TcpServer.h' \) \
  2>/dev/null

find /usr/local /usr -maxdepth 4 \
  \( -name 'libzookeeper_mt.so*' -o -name 'zookeeper.h' \) \
  2>/dev/null
```

## 3. 原始 ChatServer

使用全新目录，不复用课程工程历史 build：

```bash
cmake -S chatserver -B build/legacy-chatserver -DCMAKE_BUILD_TYPE=Debug
cmake --build build/legacy-chatserver -j"$(nproc)" --verbose
```

必须记录：

- configure 是否成功；
- 第一条真实编译/链接错误；
- 依赖版本与安装来源；
- 修复使用的命令；
- 修复后是否产生 `ChatServer` 和 `ChatClient`；
- 未启动 MySQL/Redis 时程序的真实行为。

## 4. 原始 MPRPC

```bash
cmake -S mprpc -B build/legacy-mprpc -DCMAKE_BUILD_TYPE=Debug
cmake --build build/legacy-mprpc -j"$(nproc)" --verbose
```

必须记录：

- Protobuf 生成代码与当前运行库是否兼容；
- Muduo 和 ZooKeeper C 客户端是否可找到；
- `libmprpc.a` 是否生成；
- provider/consumer 是否链接成功；
- ZooKeeper 不可用时的真实错误。

## 5. 记录格式

每次尝试记录以下字段：

```text
时间：
Git commit：
主机/系统：
命令：
退出码：
关键原始输出：
原因判断：
解决办法：
复测命令：
复测退出码：
```

不得用“预计可以”“理论通过”替代实际结果。
